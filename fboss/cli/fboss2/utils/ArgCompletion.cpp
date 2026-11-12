/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include <algorithm>
#include <cctype>

#include <folly/String.h>

namespace facebook::fboss::utils::completion {

namespace {

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return std::tolower(c);
  });
  return s;
}

// Attribute names may span several words ("timers hold-time"), so an
// attribute is matched against the typed tokens word by word.
std::vector<std::string> wordsOf(const std::string& attr) {
  std::vector<std::string> words;
  folly::split(' ', attr, words, /* ignoreEmpty */ true);
  return words;
}

// Number of words of `attr` that match typed[from...]. Equals the attr's
// word count on a full match.
size_t matchingWords(
    const std::vector<std::string>& attrWords,
    const Words& typed,
    size_t from) {
  size_t n = 0;
  while (n < attrWords.size() && from + n < typed.size() &&
         toLower(typed[from + n]) == attrWords[n]) {
    ++n;
  }
  return n;
}

} // namespace

Words completeAttrGrammar(const Words& typed, const AttrGrammar& grammar) {
  std::vector<std::pair<std::string, std::vector<std::string>>> attrs;
  attrs.reserve(grammar.attrs.size());
  for (const auto& attr : grammar.attrs) {
    attrs.emplace_back(attr, wordsOf(attr));
  }
  // Does typed[i] start some attribute?
  auto startsAttr = [&](size_t i) {
    return std::any_of(attrs.begin(), attrs.end(), [&](const auto& a) {
      return matchingWords(a.second, typed, i) > 0;
    });
  };

  // Leading free-form object tokens: everything up to the first attribute.
  // A grammar without objects has none, so its first token must be an attr.
  size_t i = 0;
  while (grammar.minObjects > 0 && i < typed.size() && !startsAttr(i)) {
    ++i;
  }
  if (i < grammar.minObjects) {
    // Still expecting object names; those are free-form.
    return {};
  }

  std::set<std::string> used;
  while (i < typed.size()) {
    // Longest attribute fully present at typed[i].
    const std::string* matched = nullptr;
    size_t matchedWords = 0;
    // Next words of attributes that typed[i..] is a proper prefix of.
    std::set<std::string> continuations;
    for (const auto& [attr, words] : attrs) {
      auto n = matchingWords(words, typed, i);
      if (n == words.size() && n > matchedWords) {
        matched = &attr;
        matchedWords = n;
      } else if (n > 0 && n < words.size() && i + n == typed.size()) {
        continuations.insert(words[n]);
      }
    }
    if (matched == nullptr) {
      // Either the attribute is incomplete (offer its next word) or the
      // token is junk the parser would reject.
      return {continuations.begin(), continuations.end()};
    }
    used.insert(*matched);
    i += matchedWords;
    if (grammar.valueless.count(*matched) != 0) {
      continue;
    }
    if (i == typed.size()) {
      // The value for the attribute is the word being completed.
      auto it = grammar.values.find(*matched);
      return it == grammar.values.end() ? Words{} : it->second;
    }
    ++i; // the value
  }

  if (!used.empty() && !grammar.repeat) {
    return {};
  }
  std::set<std::string> out;
  for (const auto& [attr, words] : attrs) {
    if (used.count(attr) == 0) {
      out.insert(words.front());
    }
  }
  return {out.begin(), out.end()};
}

Words completeAttrList(
    const Words& typed,
    const std::vector<std::string>& attrs,
    size_t minObjects) {
  AttrGrammar grammar;
  grammar.attrs = attrs;
  grammar.valueless.insert(attrs.begin(), attrs.end());
  grammar.minObjects = minObjects;
  return completeAttrGrammar(typed, grammar);
}

Words completePositions(
    const Words& typed,
    const std::vector<std::vector<std::string>>& positions) {
  if (typed.size() >= positions.size()) {
    return {};
  }
  return positions[typed.size()];
}

} // namespace facebook::fboss::utils::completion
