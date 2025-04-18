// Copyright 2022 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"

#ifdef GPR_WINDOWS

#include "src/core/lib/event_engine/windows/windows_engine.h"

absl::AnyInvocable<
    std::shared_ptr<grpc_event_engine::experimental::EventEngine>(void)>
CustomEventEngineFactory() {
  return []() {
    return std::make_shared<
        grpc_event_engine::experimental::WindowsEventEngine>();
  };
}

#else

absl::AnyInvocable<
    std::shared_ptr<grpc_event_engine::experimental::EventEngine>(void)>
CustomEventEngineFactory() {
  CHECK(false) << "This tool was not built for Windows.";
}

#endif
