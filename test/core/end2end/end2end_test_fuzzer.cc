// Copyright 2023 gRPC authors.
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

#include "test/core/end2end/end2end_test_fuzzer.h"

#include <grpc/event_engine/event_engine.h>
#include <gtest/gtest.h>
#include <stdio.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "src/core/config/config_vars.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/util/env.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/h2_tls_common.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/test_util/fuzz_config_vars.h"
#include "test/core/test_util/test_config.h"

using ::grpc_event_engine::experimental::DefaultEventEngineScope;
using ::grpc_event_engine::experimental::FuzzingEventEngine;

bool squelch = true;

namespace grpc_core {

void RunEnd2endFuzzer(const core_end2end_test_fuzzer::Msg& msg) {
  struct Test {
    std::string name;
    absl::AnyInvocable<std::unique_ptr<CoreEnd2endTest>() const> factory;
  };

  static const auto only_suite = GetEnv("GRPC_TEST_FUZZER_SUITE");
  static const auto only_test = GetEnv("GRPC_TEST_FUZZER_TEST");
  static const auto only_config = GetEnv("GRPC_TEST_FUZZER_CONFIG");

  static const auto all_tests = CoreEnd2endTestRegistry::Get().AllTests();
  static const auto tests = []() {
    g_is_fuzzing_core_e2e_tests = true;
    ForceEnableExperiment("event_engine_client", true);
    ForceEnableExperiment("event_engine_listener", true);

    std::vector<Test> tests;
    for (const auto& test : all_tests) {
      if (test.config->feature_mask & FEATURE_MASK_DO_NOT_FUZZ) continue;
      if (only_suite.has_value() && test.suite != only_suite.value()) continue;
      if (only_test.has_value() && test.name != only_test.value()) continue;
      if (only_config.has_value() && test.config->name != only_config.value()) {
        continue;
      }
      std::string test_name =
          absl::StrCat(test.suite, ".", test.name, "/", test.config->name);
      tests.emplace_back(Test{std::move(test_name), [&test]() {
                                return std::unique_ptr<CoreEnd2endTest>(
                                    test.make_test(test.config));
                              }});
    }
    std::sort(tests.begin(), tests.end(),
              [](const Test& a, const Test& b) { return a.name < b.name; });
    return tests;
  }();
  if (tests.empty()) return;

  const int test_id = msg.test_id() % tests.size();

  if (squelch && !GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    grpc_disable_all_absl_logs();
  }

  // TODO(ctiller): make this per fixture?
  ConfigVars::Overrides overrides =
      OverridesFromFuzzConfigVars(msg.config_vars());
  overrides.default_ssl_roots_file_path = CA_CERT_PATH;
  ConfigVars::SetOverrides(overrides);
  TestOnlyReloadExperimentsFromConfigVariables();
  FuzzingEventEngine::Options options;
  options.max_delay_run_after = std::chrono::milliseconds(500);
  options.max_delay_write = std::chrono::microseconds(5);
  auto engine =
      std::make_shared<FuzzingEventEngine>(options, msg.event_engine_actions());
  DefaultEventEngineScope engine_scope(engine);
  if (!squelch) {
    fprintf(stderr, "RUN TEST: %s\n", tests[test_id].name.c_str());
  }
  auto test = tests[test_id].factory();
  test->SetQuiesceEventEngine(
      [](std::shared_ptr<grpc_event_engine::experimental::EventEngine>&& ee) {
        static_cast<FuzzingEventEngine*>(ee.get())->TickUntilIdle();
      });
  test->SetCqVerifierStepFn(
      [engine = std::move(engine)](
          grpc_event_engine::experimental::EventEngine::Duration max_step) {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        engine->Tick(max_step);
        grpc_timer_manager_tick();
      });
  test->SetPostGrpcInitFunc([]() {
    grpc_timer_manager_set_threading(false);
    ExecCtx exec_ctx;
    Executor::SetThreadingAll(false);
  });
  test->SetUp();
  test->RunTest();
  test->TearDown();
  CHECK(!::testing::Test::HasFailure());
}

}  // namespace grpc_core
