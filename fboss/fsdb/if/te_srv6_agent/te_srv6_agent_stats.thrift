/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package "facebook.com/fboss/fsdb"

namespace cpp2 facebook.fboss.stats
namespace go neteng.fboss.te_srv6_agent
namespace py neteng.fboss.te_srv6_agent
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.te_srv6_agent

struct TeSrv6AgentStats {
  1: map<string, i64> fb303Counters;
}
