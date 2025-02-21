/** @file rng.cc
 *  @brief Definition of the class functions provided in rng.h including the
 *         Worker class responsible for running async operations to check if
 *         the RNG has state saved on the disk
 *
 *  @author Aashish Sheshadri
 *  @author Rohit Harchandani
 *
 *  The MIT License (MIT)
 *
 *  Copyright (c) 2015, 2016, 2017 PayPal
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

// -----------------
// standard includes
// -----------------
#include <iostream>
#include <string>
#include <vector>
#include <exception>
#include <mutex>

// ----------------------
// node.js addon includes
// ----------------------
#include <node_buffer.h>

// -----------------
// cryptopp includes
// -----------------
#include <sha3.h>
using CryptoPP::SHA3_256;

// ----------------
// library includes
// ----------------
#include <isaacRandomPool.h>

#include "rng.h"
#include "util.h"

#define MAX_ENTROPY_GEN_MULTIPLIER 6

// javascript object constructor
Nan::Persistent<v8::Function> RNG::constructor;

// -----------
// Constructor
// -----------
/**
 * Constructor
 * @brief Initilizes and constructs internal data.
 *
 * @param initCallback callback to be invoked after async
 *        operation
 * @param prng isaac RNG object pointer
 * @param fileId file identifier of RNG state on disk
 * @param digest key used to encrypt/decrypt RNG state on disk
 */
RNG::Worker::Worker(Nan::Callback* initCallback,
    IsaacRandomPool* prng,
    const std::string& fileId,
    const std::vector<uint8_t>& digest
): Nan::AsyncWorker(initCallback),
_prng(prng),
_fileId(fileId),
_digest(digest),
_isLoaded(false) {

}

RNG::Worker::Worker(Nan::Callback* initCallback,
    IsaacRandomPool* prng,
    bool isLoaded
): Nan::AsyncWorker(initCallback),
_prng(prng),
_isLoaded(isLoaded) {

}


// ----------------
// HandleOKCallback
// ----------------
/**
 * @brief Executed when the async work is complete without
 *        error, this function will be run inside the main event
 *        loop, invoking the given callback with result of the
 *        async operation of checking saved RNG state.
 *
 * The keys are returned as the second argument to the callback
 * {enc: [publicKey], dec: [privateKey]}
 *
 * @return void
 */
void RNG::Worker::HandleOKCallback () {
    Nan::HandleScope scope;
    v8::Local<v8::Object> status = Nan::New<v8::Object>();
    Nan::Set(status,
        Nan::New<v8::String>("code").ToLocalChecked(),
        Nan::New<v8::Integer>((int)_result));
    Nan::Set(status,
        Nan::New<v8::String>("message").ToLocalChecked(),
        Nan::New<v8::String>("Success").ToLocalChecked());

    v8::Local<v8::Value> argv[] = {status};
    if (callback->IsEmpty() == false) {
        callback->Call(1, argv);
    }
}



// -------------------
// HandleErrorCallback
// -------------------
/**
 * @brief Executed when the async work is complete with
 *        error, this function will be run inside the main event
 *        loop, invoking the given callback with the
 *        corresponding error.
 *
 * The error is returned as the first argument to the callback
 * {code: [statusCode], message: [errorMessage]}
 *
 * @return void
 */
void RNG::Worker::HandleErrorCallback () {
    Nan::HandleScope scope;

    v8::Local<v8::Object> error = Nan::New<v8::Object>();
    Nan::Set(error,
        Nan::New<v8::String>("code").ToLocalChecked(),
        Nan::New<v8::Integer>((int)_result));
    Nan::Set(error,
        Nan::New<v8::String>("message").ToLocalChecked(),
        Nan::New<v8::String>(ErrorMessage()).ToLocalChecked());

    v8::Local<v8::Value> argv[] = {error};
    if (callback->IsEmpty() == false) {
        callback->Call(1, argv);
    }
}



// -------
// Execute
// -------
/**
 * @brief Executed in a separate thread, asynchronously
 *        checking if RNG has saved state on the disk and
 *        communicating the status of the operation via the
 *        Worker class.
 *
 * @return void
 */
void RNG::Worker::Execute() {
    // Check if the RNG has state on disk and is initialized in memory.

    if (_isLoaded == false) {
        _result = _prng->IsInitialized(_fileId, _digest);
    } else {
        _result = _prng->SaveState();
    }

    if (_result == IsaacRandomPool::STATUS::SUCCESS) {
        return;
    }

    /* Set error message if the RNG is not initialized thus invoking the
     * error callback.
     */
    if (_result == IsaacRandomPool::STATUS::FILE_NOT_FOUND) {
        SetErrorMessage("File Not Found");
    } else if (_result == IsaacRandomPool::STATUS::DECRYPTION_ERROR) {
        SetErrorMessage("Decryption Error");
    } else{
        SetErrorMessage("Unknown Error");
    }
}



// ---
// New
// ---
/**
 * @brief Creates the node object and corresponding underlying object.
 *
 * Invoked as:
 * 'let obj = new RNG()' or
 * 'let obj = RNG()'
 *
 * @param info node.js arguments wrapper
 * @return void
 */
NAN_METHOD(RNG::New) {

  v8::Local<v8::Context> context = Nan::GetCurrentContext();

	if (info.IsConstructCall()) {

        // Invoked as constructor: 'let obj = new RNG()'.
		RNG* obj = new RNG();

		obj->Wrap(info.This());
		info.GetReturnValue().Set(info.This());

	} else {

        // Invoked as plain function `RNG()`, turn into construct call.
        const int argc = info.Length();

        // Creating argument array for construct call.
        std::vector<v8::Local<v8::Value> > argv;
        argv.reserve(argc);
        for (int i = 0; i < argc; ++i) {
            argv.push_back(info[i]);
        }

        v8::Local<v8::Function> cons = Nan::New<v8::Function>(constructor);
        info.GetReturnValue().Set(cons->NewInstance(context, argc, argv.data()));
    }
}



// -------------
// isInitialized
// -------------
/**
 * @brief Unwraps the arguments to get the key and the file name
 *        for saving the RNG state and creates an async worker to check
 *        if the RNG has been initialized by checking if the state file
 *        exists. Once the async work is complete the given callback is
 *        invoked with the result of the operation.
 *
 * Invoked as:
 * 'obj.isInitialized(key, filename, function(result){})'
 * 'key' is a buffer containing the disk encryption/decryption key
 * 'filename' is the name of the RNG saved state file on disk
 * 'result' is a js object containing the code('code') and
 *  message('message')
 *
 * @param info node.js arguments wrapper containing file id, folder
 *        path for rng state and the callback function
 *
 * @return void
 */
NAN_METHOD(RNG::isInitialized) {

    v8::Local<v8::Context> context = Nan::GetCurrentContext();

    RNG* obj = ObjectWrap::Unwrap<RNG>(info.Holder());

    // Check arguments.
    if (!node::Buffer::HasInstance(info[0])) {

        Nan::ThrowError("Incorrect Arguments. Key buffer not "
                        "provided");
        return;
    }

    /* Unwrap the first argument to get the buffer containing file
     * encryption/decryption key.
     */
    v8::Local<v8::Object> bufferObj =
        Nan::To<v8::Object>(info[0]).ToLocalChecked();

    uint8_t* bufferData = (uint8_t*)node::Buffer::Data(bufferObj);
    size_t bufferLength = node::Buffer::Length(bufferObj);

    /* Unwrap the second argument to get the file identifier of the saved
     * state on disk.
     */
    std::string fileId = "./";
    if (!info[1]->IsUndefined()) {

        v8::String::Utf8Value str(context->GetIsolate(), info[1]->ToString(context));
        fileId = *str;

    }

    /* If the size of key buffer is less than AES key size then hash the given
     * data to get key of the required size.
     */
    std::vector<uint8_t> digest;
    if (bufferLength < 32) {

        digest.resize(CryptoPP::SHA3_256::DIGESTSIZE);
        std::string bufferString(reinterpret_cast<const char*>(bufferData),
            reinterpret_cast<const char*>(bufferData) + bufferLength);
        hashString(digest, bufferString);

    } else {

        digest.reserve(CryptoPP::SHA3_256::DIGESTSIZE);
        std::copy(bufferData, bufferData + bufferLength,
            std::back_inserter(digest));
    }

    // Unwrap the third argument to get given callback function.
    Nan::Callback *callback = new Nan::Callback(info[2].As<v8::Function>());

    // Initialize the async worker and queue it.
    Worker* worker = new Worker(callback, &obj->prng, fileId, digest);

    Nan::AsyncQueueWorker(worker);

}

// ---------------
// entropyStrength
// ---------------

/**
 * @brief Returns the possible strength of entropy avaible for mining.
 *
 * @return A string with values "WEAK", "MEDIUM" or "STRONG". If the only
 *		   source of entropy is the OS this makes the module's strength
 *		   WEAK w.r.t entropy, access to either the microphone or camera
 *		   results in Medium strength and finally access to the OS, camera,
 *		   microphone and more enables STRONG strength.
 */
NAN_METHOD(RNG::entropyStrength) {
    // Get a reference to the wrapped object from the argument.
    RNG* obj = ObjectWrap::Unwrap<RNG>(info.Holder());

    std::string strength = obj->prng.EntropyStrength();
    // Return strength of underlying RNG used for key generation.
    info.GetReturnValue().Set(
        v8::String::NewFromUtf8(Nan::GetCurrentContext()->GetIsolate(),
        strength.c_str())
    );
}

// ----------
// initialize
// ----------
/**
 * @brief Unwraps the arguments to get the key and the file name
 *        for rng state and initilizes the RNG by gathering entropy.
 *
 * Invoked as:
 * 'obj.initialize(key, folder)' where
 * 'key' is a buffer containing the disk encryption/decryption key
 * 'filename' is the name of the RNG saved state file on disk
 * @param info node.js arguments wrapper containing key and filename
 *        for saving the rng state
 * @return void
 */
NAN_METHOD(RNG::initialize) {

    v8::Local<v8::Context> context = Nan::GetCurrentContext();

    RNG* obj = ObjectWrap::Unwrap<RNG>(info.Holder());

    // Check arguments
    if (!node::Buffer::HasInstance(info[0])) {

        Nan::ThrowError("Incorrect Arguments. File Identifier buffer not "
                        "provided");
        return;
    }

    /* Unwrap the first argument to get the buffer containing file
     * encryption/decryption key.
     */
    v8::Local<v8::Object> bufferObj =
        Nan::To<v8::Object>(info[0]).ToLocalChecked();

    uint8_t* bufferData = (uint8_t*)node::Buffer::Data(bufferObj);
    size_t bufferLength = node::Buffer::Length(bufferObj);

    /* Unwrap the second argument to get the file identifier of the saved
     * state on disk.
     */
    std::string fileId = "./";
    if (!info[1]->IsUndefined()) {

        v8::String::Utf8Value str(context->GetIsolate(), info[1]->ToString(context));
        fileId = *str;

    }

    /* If the size of key buffer is less than AES key size then hash the given
     * data to get key of the required size.
     */
    std::vector<uint8_t> digest;
    if (bufferLength < 32) {

        digest.resize(CryptoPP::SHA3_256::DIGESTSIZE);
        std::string bufferString(reinterpret_cast<const char*>(bufferData),
            reinterpret_cast<const char*>(bufferData) + bufferLength);
        hashString(digest, bufferString);

    } else {

        digest.reserve(CryptoPP::SHA3_256::DIGESTSIZE);
        std::copy(bufferData, bufferData + bufferLength,
            std::back_inserter(digest));
    }

    /* Initialize the global Isaac rng object and check if
     * initialization succeeded. if it fails, increase the multiplier argument
     * which causes more data to be collected to get higher entropy.
     */
    int multiplier = 0;

    try {
        for (; multiplier < MAX_ENTROPY_GEN_MULTIPLIER; ++multiplier) {

            if (obj->prng.Initialize(fileId, multiplier, digest)) {
                break;
            }
        }
    } catch (const std::exception& ex) {

        // If there is any hardware error, catch and throw the error to node.js
        Nan::ThrowError(ex.what());
        return;
    }

    // If initialization fails after max retries, throw an error to node.js.
    if (multiplier == MAX_ENTROPY_GEN_MULTIPLIER) {
        Nan::ThrowError("Not enough entropy!");
        return;
    }

    info.GetReturnValue().Set(Nan::True());

}



// --------
// getBytes
// --------
/**
 * @brief Unwraps the arguments to get the number of random bytes
 *        required and returns a buffer with the random output.
 *
 * Invoked as:
 * 'let buffer = obj.getBytes(numBytes)' where
 * 'numBytes' is the number of required random bytes
 * 'buffer' is a node.js buffer
 *
 * @param info node.js arguments wrapper containing number of random
 *        bytes required
 *
 * @return void
 */
NAN_METHOD(RNG::getBytes) {

    RNG* obj = ObjectWrap::Unwrap<RNG>(info.Holder());

    // Unwrap the first argument to get the number of required random bytes.
    uint32_t val = 0;
    if (!info[0]->IsUndefined()) {
        val = info[0]->NumberValue();
    }

    // Initialize 'output' vector containing the random bytes.
    std::vector<uint8_t> output(val);

    // Invoke 'GenerateBlock' on the isaac RNG to get the required random bytes.
    try {

        obj->prng.GenerateBlock(output.data(), val);

    } catch (const std::exception& ex) {

        // Error thrown when getBytes invoked before RNG has been initialized.
        Nan::ThrowError(ex.what());
        return;
    }

    // Copy the random bytes into node.js buffer.
    auto slowBuffer = Nan::CopyBuffer((const char*)output.data(),
        val).ToLocalChecked();

    // Set node.js buffer as return value of the function.
    info.GetReturnValue().Set(slowBuffer);
}



// ---------
// saveState
// ---------
/**
 * @brief Encryptes and saves the state of the RNG to disk
 *
 * Invoked as:
 * 'obj.saveState(function (result) {})'
 * 'result' is a js object containing the code('code') and
 *  message('message')
 *
 * @return void
 */
NAN_METHOD(RNG::saveState) {
    RNG* obj = ObjectWrap::Unwrap<RNG>(info.Holder());

    // Unwrap the first argument to get given callback function.
    Nan::Callback *callback = new Nan::Callback(info[0].As<v8::Function>());

    // Initialize the async worker and queue it.
    Worker* worker = new Worker(callback, &obj->prng, true);

    Nan::AsyncQueueWorker(worker);
}



// -------
// destroy
// -------
/**
 * @brief Destroys the underlying RNG object thus saving the state
 *        to disk.
 *
 * Invoked as:
 * 'obj.destroy()'
 *
 * @return void
 */
NAN_METHOD(RNG::destroy) {
    RNG* obj = ObjectWrap::Unwrap<RNG>(info.Holder());

    obj->prng.Destroy();
}



// ----
// Init
// ----
/**
 * @brief Initialization function for node.js object wrapper exported
 *        by the addon.
 *
 * @param exports node.js module exports
 *
 * @return void
 */
void RNG::Init(v8::Local<v8::Object> exports) {

    v8::Local<v8::Context> context = Nan::GetCurrentContext();

    Nan::HandleScope scope;

    // Prepare constructor template.
    v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
    tpl->SetClassName(Nan::New("RNG").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(6);

    // Prototype
    Nan::SetPrototypeMethod(tpl, "getBytes", getBytes);
    Nan::SetPrototypeMethod(tpl, "isInitialized", isInitialized);
    Nan::SetPrototypeMethod(tpl, "entropyStrength", entropyStrength);
    Nan::SetPrototypeMethod(tpl, "initialize", initialize);
    Nan::SetPrototypeMethod(tpl, "saveState", saveState);
    Nan::SetPrototypeMethod(tpl, "destroy", destroy);

    constructor.Reset(context->GetIsolate(), tpl->GetFunction(context));

    // Setting node.js module.exports.
    exports->Set(context, Nan::New("RNG").ToLocalChecked(), tpl->GetFunction(context));
}
