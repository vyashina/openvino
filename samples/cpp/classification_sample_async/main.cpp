// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

/**
 * @brief The entry point the OpenVINO Runtime sample application
 * @file classification_sample_async/main.cpp
 * @example classification_sample_async/main.cpp
 */

#include <format_reader_ptr.h>
#include <samples/classification_results.h>
#include <sys/stat.h>

#include <condition_variable>
#include <fstream>
#include <inference_engine.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <samples/args_helper.hpp>
#include <samples/common.hpp>
#include <samples/slog.hpp>
#include <string>
#include <vector>

#include "classification_sample_async.h"
#include "openvino/openvino.hpp"

/**
 * @brief Checks input args
 * @param argc number of args
 * @param argv list of input arguments
 * @return bool status true(Success) or false(Fail)
 */
bool ParseAndCheckCommandLine(int argc, char* argv[]) {
    gflags::ParseCommandLineNonHelpFlags(&argc, &argv, true);
    if (FLAGS_h) {
        showUsage();
        showAvailableDevices();
        return false;
    }
    slog::info << "Parsing input parameters" << slog::endl;

    if (FLAGS_nt <= 0) {
        throw std::logic_error("Incorrect value for nt argument. It should be greater than 0.");
    }

    if (FLAGS_m.empty()) {
        showUsage();
        throw std::logic_error("Model is required but not set. Please set -m option.");
    }

    if (FLAGS_i.empty()) {
        showUsage();
        throw std::logic_error("Input is required but not set. Please set -i option.");
    }

    return true;
}

int main(int argc, char* argv[]) {
    try {
        // -------- Get OpenVINO Runtime version --------
        slog::info << ov::get_openvino_version() << slog::endl;

        // -------- Parsing and validation of input arguments --------
        if (!ParseAndCheckCommandLine(argc, argv)) {
            return EXIT_SUCCESS;
        }

        // -------- Read input --------
        // This vector stores paths to the processed images
        std::vector<std::string> image_names;
        parseInputFilesArguments(image_names);
        if (image_names.empty())
            throw std::logic_error("No suitable images were found");

        // -------- Step 1. Initialize OpenVINO Runtime Core --------
        ov::runtime::Core core;

        if (!FLAGS_l.empty()) {
            auto extension_ptr = std::make_shared<InferenceEngine::Extension>(FLAGS_l);
            core.add_extension(extension_ptr);
            slog::info << "Extension loaded: " << FLAGS_l << slog::endl;
        }
        if (!FLAGS_c.empty() && (FLAGS_d == "GPU" || FLAGS_d == "MYRIAD" || FLAGS_d == "HDDL")) {
            // Config for device plugin custom extension is loaded from an .xml
            // description
            core.set_config({{InferenceEngine::PluginConfigParams::KEY_CONFIG_FILE, FLAGS_c}}, FLAGS_d);
            slog::info << "Config for " << FLAGS_d << " device plugin custom extension loaded: " << FLAGS_c
                       << slog::endl;
        }

        // -------- Step 2. Read a model --------
        slog::info << "Loading model files:" << slog::endl << FLAGS_m << slog::endl;
        std::shared_ptr<ov::Function> model = core.read_model(FLAGS_m);

        OPENVINO_ASSERT(model->get_parameters().size() == 1, "Sample supports models with 1 input only");
        OPENVINO_ASSERT(model->get_results().size() == 1, "Sample supports models with 1 output only");

        // -------- Step 3. Apply preprocessing --------
        const ov::Layout tensor_layout{"NHWC"};

        ov::preprocess::PrePostProcessor proc(model);
        // 1) input() with no args assumes a model has a single input
        ov::preprocess::InputInfo& input_info = proc.input();
        // 2) Set input tensor information:
        // - precision of tensor is supposed to be 'u8'
        // - layout of data is 'NHWC'
        input_info.tensor().set_element_type(ov::element::u8).set_layout(tensor_layout);
        // 3) Here we suppose model has 'NCHW' layout for input
        input_info.model().set_layout("NCHW");
        // 4) output() with no args assumes a model has a single result
        // - output() with no args assumes a model has a single result
        // - precision of tensor is supposed to be 'f32'
        proc.output().tensor().set_element_type(ov::element::f32);
        // 5) Once the build() method is called, the pre(post)processing steps
        // for layout and precision conversions are inserted automatically
        model = proc.build();

        // -------- Step 4. read input images --------
        slog::info << "Read input images" << slog::endl;

        ov::Shape input_shape = model->input().get_shape();
        const size_t width = input_shape[ov::layout::width_idx(tensor_layout)];
        const size_t height = input_shape[ov::layout::height_idx(tensor_layout)];

        std::vector<std::shared_ptr<unsigned char>> images_data;
        std::vector<std::string> valid_image_names;
        for (const auto& i : image_names) {
            FormatReader::ReaderPtr reader(i.c_str());
            if (reader.get() == nullptr) {
                slog::warn << "Image " + i + " cannot be read!" << slog::endl;
                continue;
            }
            // Store image data
            std::shared_ptr<unsigned char> data(reader->getData(width, height));
            if (data != nullptr) {
                images_data.push_back(data);
                valid_image_names.push_back(i);
            }
        }
        if (images_data.empty() || valid_image_names.empty())
            throw std::logic_error("Valid input images were not found!");

        // -------- Step 5. Loading model to the device --------
        // Setting batch size using image count
        const size_t batchSize = images_data.size();
        input_shape[ov::layout::batch_idx(tensor_layout)] = batchSize;
        model->reshape({{model->input().get_any_name(), input_shape}});
        slog::info << "Batch size is " << std::to_string(batchSize) << slog::endl;

        // -------- Step 6. Loading model to the device --------
        slog::info << "Loading model to the device " << FLAGS_d << slog::endl;
        ov::runtime::ExecutableNetwork executable_network = core.compile_model(model, FLAGS_d);

        // -------- Step 6. Create infer request --------
        slog::info << "Create infer request" << slog::endl;
        ov::runtime::InferRequest infer_request = executable_network.create_infer_request();

        // -------- Step 7. Combine multiple input images as batch --------
        ov::runtime::Tensor input_tensor = infer_request.get_input_tensor();

        for (size_t image_id = 0; image_id < images_data.size(); ++image_id) {
            const size_t image_size = shape_size(input_shape) / batchSize;
            std::memcpy(input_tensor.data<std::uint8_t>() + image_id * image_size,
                        images_data[image_id].get(),
                        image_size);
        }

        // -------- Step 8. Do asynchronous inference --------
        size_t num_iterations = 10;
        size_t cur_iteration = 0;
        std::condition_variable condVar;
        std::mutex mutex;

        infer_request.set_callback([&](std::exception_ptr ex) {
            if (ex)
                throw ex;
            std::lock_guard<std::mutex> l(mutex);
            cur_iteration++;
            slog::info << "Completed " << cur_iteration << " async request execution" << slog::endl;
            if (cur_iteration < num_iterations) {
                /* here a user can read output containing inference results and put new
                   input to repeat async request again */
                infer_request.start_async();
            } else {
                /* continue sample execution after last Asynchronous inference request
                 * execution */
                condVar.notify_one();
            }
        });

        /* Start async request for the first time */
        slog::info << "Start inference (" << num_iterations << " asynchronous executions)" << slog::endl;
        infer_request.start_async();

        /* Wait all iterations of the async request */
        std::unique_lock<std::mutex> lock(mutex);
        condVar.wait(lock, [&] {
            return cur_iteration == num_iterations;
        });

        // -------- Step 9. Process output --------
        ov::runtime::Tensor output = infer_request.get_output_tensor();

        /** Validating -nt value **/
        const size_t resultsCnt = output.get_size() / batchSize;
        if (FLAGS_nt > resultsCnt || FLAGS_nt < 1) {
            slog::warn << "-nt " << FLAGS_nt << " is not available for this model (-nt should be less than "
                       << resultsCnt + 1 << " and more than 0)\n            Maximal value " << resultsCnt
                       << " will be used." << slog::endl;
            FLAGS_nt = resultsCnt;
        }

        /** Read labels from file (e.x. AlexNet.labels) **/
        std::string labelFileName = fileNameNoExt(FLAGS_m) + ".labels";
        std::vector<std::string> labels;

        std::ifstream inputFile;
        inputFile.open(labelFileName, std::ios::in);
        if (inputFile.is_open()) {
            std::string strLine;
            while (std::getline(inputFile, strLine)) {
                trim(strLine);
                labels.push_back(strLine);
            }
        }

        // Prints formatted classification results
        ClassificationResult classificationResult(output, valid_image_names, batchSize, FLAGS_nt, labels);
        classificationResult.show();
    } catch (const std::exception& error) {
        slog::err << error.what() << slog::endl;
        return EXIT_FAILURE;
    } catch (...) {
        slog::err << "Unknown/internal exception happened." << slog::endl;
        return EXIT_FAILURE;
    }

    slog::info << "Execution successful" << slog::endl;
    slog::info << slog::endl
               << "This sample is an API example, for any performance measurements "
                  "please use the dedicated benchmark_app tool"
               << slog::endl;
    return EXIT_SUCCESS;
}