# Copyright 2024 the gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

licenses(["notice"])

cc_binary(
    name = "client_flow_control_client",
    srcs = ["client_flow_control_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//:grpc++_reflection",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:initialize",
    ],
)

cc_binary(
    name = "client_flow_control_server",
    srcs = ["client_flow_control_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//:grpc++_reflection",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:initialize",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_binary(
    name = "server_flow_control_client",
    srcs = ["server_flow_control_client.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//:grpc++_reflection",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:initialize",
    ],
)

cc_binary(
    name = "server_flow_control_server",
    srcs = ["server_flow_control_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//:grpc++",
        "//:grpc++_reflection",
        "//examples/protos:helloworld_cc_grpc",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:initialize",
    ],
)
