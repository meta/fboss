/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <thrift/lib/cpp/util/EnumUtils.h>

/*
 * Shell-completion helpers for the positional arguments of a command.
 *
 * Most config/delete commands keep their leaf vocabulary (attribute names,
 * enum values) in a list inside one handler rather than as one CLI11
 * subcommand per token, so the shell cannot discover those tokens by walking
 * the CLI11 tree. A command opts into completing them by adding
 *
 *   static std::vector<std::string> completeArgs(
 *       const std::vector<std::string>& typed);
 *
 * to its Traits. `typed` holds the positional tokens already present on the
 * command line after the last sub-command (never the partial word being
 * completed); the function returns every token that may come next. The shell
 * filters by prefix, so callers return the full candidate set.
 *
 * The helpers below express the grammars that recur across the command tree
 * so that a Traits usually needs a single line, e.g.
 *
 *   return utils::completion::completeAttrGrammar(
 *       typed, {.attrs = {"timeout", "max-probes"}});
 */
namespace facebook::fboss::utils::completion {

using Words = std::vector<std::string>;

/*
 * Grammar: [<object> ...] <attr> [<value>] [<attr> [<value>] ...]
 *
 * - `minObjects` free-form tokens (names, ids, port lists) precede the first
 *   attribute. Tokens before the first known attribute are objects, exactly
 *   as MultiArgsConfigType::parseTokens() splits them.
 * - Every attribute takes one value unless listed in `valueless`.
 * - `values` maps an attribute to the finite set of values it accepts; other
 *   attributes take free-form values, for which nothing is offered.
 * - With `repeat` false only one <attr> [<value>] group is accepted.
 * - An attribute already present on the line is not offered again.
 */
struct AttrGrammar {
  std::vector<std::string> attrs;
  std::set<std::string> valueless;
  std::map<std::string, std::vector<std::string>> values;
  size_t minObjects = 0;
  bool repeat = true;
};

Words completeAttrGrammar(const Words& typed, const AttrGrammar& grammar);

/*
 * Grammar: [<object> ...] <attr> [<attr> ...]
 *
 * The delete-command shape: a list of attribute names, none of which take a
 * value, optionally preceded by `minObjects` free-form tokens.
 */
Words completeAttrList(
    const Words& typed,
    const std::vector<std::string>& attrs,
    size_t minObjects = 0);

/*
 * Grammar: a fixed sequence of positions, each with its own candidate set.
 * An empty set means the position takes a free-form value. Nothing is offered
 * past the last position.
 */
Words completePositions(
    const Words& typed,
    const std::vector<std::vector<std::string>>& positions);

// The names of a thrift enum, for attributes whose value is parsed with
// TEnumTraits<EnumT>::findValue.
template <typename EnumT>
std::vector<std::string> enumNames() {
  std::vector<std::string> names;
  for (auto value : apache::thrift::TEnumTraits<EnumT>::values) {
    names.push_back(apache::thrift::util::enumNameSafe(value));
  }
  return names;
}

// Convenience for building `attrs` from the constexpr string_view arrays
// that config commands typically keep their vocabulary in.
template <typename Container>
std::vector<std::string> toWords(const Container& c) {
  std::vector<std::string> out;
  out.reserve(std::size(c));
  for (const auto& s : c) {
    out.emplace_back(s);
  }
  return out;
}

} // namespace facebook::fboss::utils::completion
