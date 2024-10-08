// Copyright (c) 2008, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "client/windows/crash_generation/crash_generation_client.h"
#include <cassert>
#include <utility>
#include "client/windows/common/ipc_protocol.h"

namespace google_breakpad {

const int kPipeBusyWaitTimeoutMs = 2000;

#ifdef _DEBUG
const DWORD kWaitForServerTimeoutMs = INFINITE;
#else
const DWORD kWaitForServerTimeoutMs = 15000;
#endif

const int kPipeConnectMaxAttempts = 2;

const DWORD kPipeDesiredAccess = FILE_READ_DATA |
                                 FILE_WRITE_DATA |
                                 FILE_WRITE_ATTRIBUTES;

const DWORD kPipeFlagsAndAttributes = SECURITY_IDENTIFICATION |
                                      SECURITY_SQOS_PRESENT;

const DWORD kPipeMode = PIPE_READMODE_MESSAGE;

const size_t kWaitEventCount = 2;

// This function is orphan for production code. It can be used
// for debugging to help repro some scenarios like the client
// is slow in writing to the pipe after connecting, the client
// is slow in reading from the pipe after writing, etc. The parameter
// overlapped below is not used and it is present to match the signature
// of this function to TransactNamedPipe Win32 API. Uncomment if needed
// for debugging.
/**
static bool TransactNamedPipeDebugHelper(HANDLE pipe,
                                         const void* in_buffer,
                                         DWORD in_size,
                                         void* out_buffer,
                                         DWORD out_size,
                                         DWORD* bytes_count,
                                         LPOVERLAPPED) {
  // Uncomment the next sleep to create a gap before writing
  // to pipe.
  // Sleep(5000);

  if (!WriteFile(pipe,
                 in_buffer,
                 in_size,
                 bytes_count,
                 nullptr)) {
    return false;
  }

  // Uncomment the next sleep to create a gap between write
  // and read.
  // Sleep(5000);

  return ReadFile(pipe, out_buffer, out_size, bytes_count, nullptr) != FALSE;
}
**/

CrashGenerationClient::CrashGenerationClient(
    const wchar_t* pipe_name,
    MINIDUMP_TYPE dump_type,
    const CustomClientInfo* custom_info)
        : pipe_name_(pipe_name),
          dump_type_(dump_type),
          thread_id_(0),
          server_process_id_(0),
          crash_event_(nullptr),
          crash_generated_(nullptr),
          server_alive_(nullptr),
          exception_pointers_(nullptr),
          custom_info_() {
  memset(&assert_info_, 0, sizeof(assert_info_));
  if (custom_info) {
    custom_info_ = *custom_info;
  }
}

CrashGenerationClient::~CrashGenerationClient() {
  if (crash_event_) {
    CloseHandle(crash_event_);
  }

  if (crash_generated_) {
    CloseHandle(crash_generated_);
  }

  if (server_alive_) {
    CloseHandle(server_alive_);
  }
}

// Performs the registration step with the server process.
// The registration step involves communicating with the server
// via a named pipe. The client sends the following pieces of
// data to the server:
//
// * Message tag indicating the client is requesting registration.
// * Process id of the client process.
// * Address of a DWORD variable in the client address space
//   that will contain the thread id of the client thread that
//   caused the crash.
// * Address of a EXCEPTION_POINTERS* variable in the client
//   address space that will point to an instance of EXCEPTION_POINTERS
//   when the crash happens.
// * Address of an instance of MDRawAssertionInfo that will contain
//   relevant information in case of non-exception crashes like assertion
//   failures and pure calls.
//
// In return the client expects the following information from the server:
//
// * Message tag indicating successful registration.
// * Server process id.
// * Handle to an object that client can signal to request dump
//   generation from the server.
// * Handle to an object that client can wait on after requesting
//   dump generation for the server to finish dump generation.
// * Handle to a mutex object that client can wait on to make sure
//   server is still alive.
//
// If any step of the expected behavior mentioned above fails, the
// registration step is not considered successful and hence out-of-process
// dump generation service is not available.
//
// Returns true if the registration is successful; false otherwise.
bool CrashGenerationClient::Register() {
  HANDLE pipe = ConnectToServer();
  if (!pipe) {
    return false;
  }

  bool success = RegisterClient(pipe);
  CloseHandle(pipe);
  return success;
}

HANDLE CrashGenerationClient::ConnectToServer() {
  HANDLE pipe = ConnectToPipe(pipe_name_.c_str(),
                              kPipeDesiredAccess,
                              kPipeFlagsAndAttributes);
  if (!pipe) {
    return nullptr;
  }

  DWORD mode = kPipeMode;
  if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
    CloseHandle(pipe);
    pipe = nullptr;
  }

  return pipe;
}

bool CrashGenerationClient::RegisterClient(HANDLE pipe) {
  ProtocolMessage msg(MESSAGE_TAG_REGISTRATION_REQUEST,
                      GetCurrentProcessId(),
                      dump_type_,
                      &thread_id_,
                      &exception_pointers_,
                      &assert_info_,
                      custom_info_,
                      nullptr,
                      nullptr,
                      nullptr);
  ProtocolMessage reply;
  DWORD bytes_count = 0;
  // The call to TransactNamedPipe below can be changed to a call
  // to TransactNamedPipeDebugHelper to help repro some scenarios.
  // For details see comments for TransactNamedPipeDebugHelper.
  if (!TransactNamedPipe(pipe,
                         &msg,
                         sizeof(msg),
                         &reply,
                         sizeof(ProtocolMessage),
                         &bytes_count,
                         nullptr)) {
    return false;
  }

  if (!ValidateResponse(reply)) {
    return false;
  }

  ProtocolMessage ack_msg;
  ack_msg.tag = MESSAGE_TAG_REGISTRATION_ACK;

  if (!WriteFile(pipe, &ack_msg, sizeof(ack_msg), &bytes_count, nullptr)) {
    return false;
  }
  crash_event_ = reply.dump_request_handle;
  crash_generated_ = reply.dump_generated_handle;
  server_alive_ = reply.server_alive_handle;
  server_process_id_ = reply.pid;

  return true;
}

HANDLE CrashGenerationClient::ConnectToPipe(const wchar_t* pipe_name,
                                            DWORD pipe_access,
                                            DWORD flags_attrs) {
  for (int i = 0; i < kPipeConnectMaxAttempts; ++i) {
    HANDLE pipe = CreateFile(pipe_name,
                             pipe_access,
                             0,
                             nullptr,
                             OPEN_EXISTING,
                             flags_attrs,
                             nullptr);
    if (pipe != INVALID_HANDLE_VALUE) {
      return pipe;
    }

    // Cannot continue retrying if error is something other than
    // ERROR_PIPE_BUSY.
    if (GetLastError() != ERROR_PIPE_BUSY) {
      break;
    }

    // Cannot continue retrying if wait on pipe fails.
    if (!WaitNamedPipe(pipe_name, kPipeBusyWaitTimeoutMs)) {
      break;
    }
  }

  return nullptr;
}

bool CrashGenerationClient::ValidateResponse(
    const ProtocolMessage& msg) const {
  return (msg.tag == MESSAGE_TAG_REGISTRATION_RESPONSE) &&
         (msg.pid != 0) &&
         (msg.dump_request_handle != nullptr) &&
         (msg.dump_generated_handle != nullptr) &&
         (msg.server_alive_handle != nullptr);
}

bool CrashGenerationClient::IsRegistered() const {
  return crash_event_ != nullptr;
}

bool CrashGenerationClient::RequestDump(EXCEPTION_POINTERS* ex_info) {
  if (!IsRegistered()) {
    return false;
  }

  exception_pointers_ = ex_info;
  thread_id_ = GetCurrentThreadId();

  assert_info_.line = 0;
  assert_info_.type = 0;
  assert_info_.expression[0] = 0;
  assert_info_.file[0] = 0;
  assert_info_.function[0] = 0;

  return SignalCrashEventAndWait();
}

bool CrashGenerationClient::RequestDump(MDRawAssertionInfo* assert_info) {
  if (!IsRegistered()) {
    return false;
  }

  exception_pointers_ = nullptr;

  if (assert_info) {
    memcpy(&assert_info_, assert_info, sizeof(assert_info_));
  } else {
    memset(&assert_info_, 0, sizeof(assert_info_));
  }

  thread_id_ = GetCurrentThreadId();

  return SignalCrashEventAndWait();
}

bool CrashGenerationClient::SignalCrashEventAndWait() {
  assert(crash_event_);
  assert(crash_generated_);
  assert(server_alive_);

  // Reset the dump generated event before signaling the crash
  // event so that the server can set the dump generated event
  // once it is done generating the event.
  if (!ResetEvent(crash_generated_)) {
    return false;
  }

  if (!SetEvent(crash_event_)) {
    return false;
  }

  HANDLE wait_handles[kWaitEventCount] = {crash_generated_, server_alive_};

  DWORD result = WaitForMultipleObjects(kWaitEventCount,
                                        wait_handles,
                                        FALSE,
                                        kWaitForServerTimeoutMs);

  // Crash dump was successfully generated only if the server
  // signaled the crash generated event.
  return result == WAIT_OBJECT_0;
}

}  // namespace google_breakpad
