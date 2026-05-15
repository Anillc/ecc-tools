// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file SdcClockReader.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-15
 * @brief Side-effect-free SDC clock subset reader implementation for iCTS.
 */

#include "SdcClockReader.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "Log.hh"
#include "dm_config.h"
#include "idm.h"
#include "logger/Schema.hh"

namespace icts {
namespace {

constexpr std::size_t kCommandSubstitutionLimit = 1024U;

struct ParsedWord
{
  std::string text;
  bool braced = false;
};

struct SdcValue
{
  std::vector<std::string> strings;
  std::vector<SdcObjectRef> objects;
};

auto configuredSdcPath() -> std::string
{
  return dmInst->get_config().get_sdc_path();
}

auto trim(const std::string& text) -> std::string
{
  const auto first = std::ranges::find_if_not(text, [](unsigned char ch) -> bool { return std::isspace(ch) != 0; });
  if (first == text.end()) {
    return {};
  }
  const auto last
      = std::ranges::find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) -> bool { return std::isspace(ch) != 0; }).base();
  return std::string(first, last);
}

auto isOption(const std::string& text) -> bool
{
  return text.size() > 1U && text.front() == '-';
}

auto joinStrings(const std::vector<std::string>& values) -> std::string
{
  std::string result;
  for (const auto& value : values) {
    if (!result.empty()) {
      result += ' ';
    }
    result += value;
  }
  return result;
}

auto splitListText(const std::string& text) -> std::vector<std::string>
{
  std::vector<std::string> values;
  std::istringstream stream(text);
  std::string value;
  while (stream >> value) {
    values.push_back(value);
  }
  if (values.empty() && !text.empty()) {
    values.push_back(text);
  }
  return values;
}

auto toLower(std::string text) -> std::string
{
  std::ranges::transform(text, text.begin(), [](unsigned char ch) -> char { return static_cast<char>(std::tolower(ch)); });
  return text;
}

auto valueToString(const SdcValue& value) -> std::string
{
  if (!value.objects.empty()) {
    std::vector<std::string> patterns;
    patterns.reserve(value.objects.size());
    std::ranges::transform(value.objects, std::back_inserter(patterns),
                           [](const SdcObjectRef& object) -> std::string { return object.pattern; });
    return joinStrings(patterns);
  }
  return joinStrings(value.strings);
}

auto makeStringValue(std::string text) -> SdcValue
{
  SdcValue value;
  value.strings.push_back(std::move(text));
  return value;
}

auto makeObjectValue(SdcObjectKind kind, const std::vector<std::string>& patterns) -> SdcValue
{
  SdcValue value;
  value.objects.reserve(patterns.size());
  for (const auto& pattern : patterns) {
    if (pattern.empty()) {
      continue;
    }
    value.objects.emplace_back(SdcObjectRef{kind, pattern, true});
  }
  return value;
}

auto appendRefsFromValue(std::vector<SdcObjectRef>& refs, const SdcValue& value, SdcObjectKind fallback_kind) -> void
{
  if (!value.objects.empty()) {
    refs.insert(refs.end(), value.objects.begin(), value.objects.end());
    return;
  }
  for (const auto& text : value.strings) {
    for (const auto& item : splitListText(text)) {
      refs.emplace_back(SdcObjectRef{fallback_kind, item, false});
    }
  }
}

auto parseDoubleValue(const std::string& text, double& value) -> bool
{
  const auto clean_text = trim(text);
  if (clean_text.empty()) {
    return false;
  }
  std::istringstream stream(clean_text);
  stream >> value;
  stream >> std::ws;
  return !stream.fail() && stream.eof();
}

auto parseIntValue(const std::string& text, int& value) -> bool
{
  const auto clean_text = trim(text);
  if (clean_text.empty()) {
    return false;
  }
  long parsed = 0;
  std::istringstream stream(clean_text);
  stream >> parsed;
  stream >> std::ws;
  if (stream.fail() || !stream.eof()) {
    return false;
  }
  value = static_cast<int>(parsed);
  return true;
}

auto timeUnitToNs(const std::string& unit) -> double
{
  const auto normalized = toLower(trim(unit));
  const std::map<std::string, double> scale_by_unit = {
      {"fs", 0.000001}, {"ps", 0.001}, {"ns", 1.0}, {"us", 1000.0}, {"ms", 1000000.0}, {"s", 1000000000.0},
  };
  if (const auto iter = scale_by_unit.find(normalized); iter != scale_by_unit.end()) {
    return iter->second;
  }

  std::size_t suffix_pos = normalized.size();
  while (suffix_pos > 0U && std::isalpha(static_cast<unsigned char>(normalized[suffix_pos - 1U])) != 0) {
    --suffix_pos;
  }
  if (suffix_pos == normalized.size() || suffix_pos == 0U) {
    return 1.0;
  }
  const auto suffix = normalized.substr(suffix_pos);
  const auto suffix_iter = scale_by_unit.find(suffix);
  if (suffix_iter == scale_by_unit.end()) {
    return 1.0;
  }
  double numeric_scale = 0.0;
  if (!parseDoubleValue(normalized.substr(0U, suffix_pos), numeric_scale)) {
    return 1.0;
  }
  return numeric_scale * suffix_iter->second;
}

class ArithmeticParser
{
 public:
  explicit ArithmeticParser(std::string expression) : _expression(std::move(expression)) {}

  auto parse(double& value) -> bool
  {
    _pos = 0U;
    std::vector<double> values;
    std::vector<char> operators;
    bool expect_value = true;

    while (true) {
      skipSpaces();
      if (_pos >= _expression.size()) {
        break;
      }
      if (matchWord("double")) {
        _pos += 6U;
        continue;
      }

      const char token = _expression[_pos];
      if (token == '(') {
        operators.push_back(token);
        ++_pos;
        expect_value = true;
        continue;
      }
      if (token == ')') {
        if (!applyUntilOpen(values, operators)) {
          return false;
        }
        ++_pos;
        expect_value = false;
        continue;
      }
      if (isOperator(token)) {
        if (expect_value) {
          if (token == '+') {
            ++_pos;
            continue;
          }
          if (token == '-') {
            ++_pos;
            skipSpaces();
            if (_pos < _expression.size() && _expression[_pos] == '(') {
              values.push_back(0.0);
              while (!operators.empty() && operators.back() != '(' && precedence(operators.back()) >= precedence('-')) {
                if (!applyTopOperator(values, operators)) {
                  return false;
                }
              }
              operators.push_back('-');
              continue;
            }
            double number = 0.0;
            if (!parseNumber(number)) {
              return false;
            }
            values.push_back(-number);
            expect_value = false;
            continue;
          }
          return false;
        }
        while (!operators.empty() && operators.back() != '(' && precedence(operators.back()) >= precedence(token)) {
          if (!applyTopOperator(values, operators)) {
            return false;
          }
        }
        operators.push_back(token);
        ++_pos;
        expect_value = true;
        continue;
      }

      double number = 0.0;
      if (!parseNumber(number)) {
        return false;
      }
      values.push_back(number);
      expect_value = false;
    }

    if (expect_value || values.empty()) {
      return false;
    }
    while (!operators.empty()) {
      if (operators.back() == '(' || !applyTopOperator(values, operators)) {
        return false;
      }
    }
    if (values.size() != 1U) {
      return false;
    }
    value = values.back();
    return true;
  }

 private:
  static auto isOperator(char token) -> bool { return token == '+' || token == '-' || token == '*' || token == '/'; }

  static auto precedence(char token) -> int
  {
    if (token == '*' || token == '/') {
      return 2;
    }
    return token == '+' || token == '-' ? 1 : 0;
  }

  static auto applyTopOperator(std::vector<double>& values, std::vector<char>& operators) -> bool
  {
    if (values.size() < 2U || operators.empty()) {
      return false;
    }
    const auto op = operators.back();
    operators.pop_back();
    const auto rhs = values.back();
    values.pop_back();
    const auto lhs = values.back();
    values.pop_back();

    if (op == '+') {
      values.push_back(lhs + rhs);
    } else if (op == '-') {
      values.push_back(lhs - rhs);
    } else if (op == '*') {
      values.push_back(lhs * rhs);
    } else if (op == '/') {
      if (rhs == 0.0) {
        return false;
      }
      values.push_back(lhs / rhs);
    } else {
      return false;
    }
    return true;
  }

  static auto applyUntilOpen(std::vector<double>& values, std::vector<char>& operators) -> bool
  {
    while (!operators.empty() && operators.back() != '(') {
      if (!applyTopOperator(values, operators)) {
        return false;
      }
    }
    if (operators.empty() || operators.back() != '(') {
      return false;
    }
    operators.pop_back();
    return true;
  }

  auto parseNumber(double& value) -> bool
  {
    skipSpaces();
    const auto start = _pos;
    bool has_digit = false;
    while (_pos < _expression.size()) {
      const auto ch = _expression[_pos];
      if (std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '.') {
        has_digit = true;
        ++_pos;
        continue;
      }
      if ((ch == 'e' || ch == 'E') && _pos + 1U < _expression.size()) {
        ++_pos;
        if (_expression[_pos] == '+' || _expression[_pos] == '-') {
          ++_pos;
        }
        continue;
      }
      break;
    }
    if (!has_digit) {
      return false;
    }
    return parseDoubleValue(_expression.substr(start, _pos - start), value);
  }

  auto skipSpaces() -> void
  {
    while (_pos < _expression.size() && std::isspace(static_cast<unsigned char>(_expression[_pos])) != 0) {
      ++_pos;
    }
  }

  auto matchWord(std::string_view word) const -> bool
  {
    if (_expression.size() - _pos < word.size()) {
      return false;
    }
    if (_expression.compare(_pos, word.size(), word) != 0) {
      return false;
    }
    const auto next_pos = _pos + word.size();
    return next_pos == _expression.size() || std::isalnum(static_cast<unsigned char>(_expression[next_pos])) == 0;
  }

  std::string _expression;
  std::size_t _pos = 0U;
};

class SdcSubsetEvaluator
{
 public:
  auto readFile(const std::string& sdc_path) -> SdcClockData
  {
    std::ifstream stream(sdc_path);
    if (!stream.is_open()) {
      _data.diagnostics.emplace_back("failed_to_open_sdc_file");
      return _data;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    for (const auto& command : splitCommands(buffer.str())) {
      (void) evaluateCommand(command);
    }
    return _data;
  }

 private:
  static auto splitCommands(const std::string& text) -> std::vector<std::string>
  {
    std::vector<std::string> commands;
    std::string command;
    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_quote = false;
    bool escaped = false;
    bool command_word_start = true;

    for (std::size_t index = 0U; index < text.size(); ++index) {
      const char ch = text[index];
      if (escaped) {
        command += ch;
        escaped = false;
        command_word_start = false;
        continue;
      }
      if (ch == '\\') {
        command += ch;
        escaped = true;
        continue;
      }
      if (!in_quote && brace_depth == 0 && bracket_depth == 0 && ch == '#' && command_word_start) {
        while (index < text.size() && text[index] != '\n') {
          ++index;
        }
        if (index < text.size()) {
          const auto clean_command = trim(command);
          if (!clean_command.empty()) {
            commands.emplace_back(clean_command);
          }
          command.clear();
          command_word_start = true;
        }
        continue;
      }
      if (ch == '"' && brace_depth == 0) {
        in_quote = !in_quote;
        command += ch;
        command_word_start = false;
        continue;
      }
      if (!in_quote) {
        if (ch == '{') {
          ++brace_depth;
        } else if (ch == '}' && brace_depth > 0) {
          --brace_depth;
        } else if (ch == '[') {
          ++bracket_depth;
        } else if (ch == ']' && bracket_depth > 0) {
          --bracket_depth;
        }
      }
      if (!in_quote && brace_depth == 0 && bracket_depth == 0 && (ch == '\n' || ch == ';')) {
        const auto clean_command = trim(command);
        if (!clean_command.empty()) {
          commands.emplace_back(clean_command);
        }
        command.clear();
        command_word_start = true;
        continue;
      }
      command += ch;
      command_word_start = command_word_start && std::isspace(static_cast<unsigned char>(ch)) != 0;
    }

    const auto clean_command = trim(command);
    if (!clean_command.empty()) {
      commands.emplace_back(clean_command);
    }
    return commands;
  }

  static auto parseWords(const std::string& command) -> std::vector<ParsedWord>
  {
    std::vector<ParsedWord> words;
    std::size_t index = 0U;
    while (index < command.size()) {
      while (index < command.size() && std::isspace(static_cast<unsigned char>(command[index])) != 0) {
        ++index;
      }
      if (index >= command.size()) {
        break;
      }
      if (command[index] == '{') {
        auto [word_text, end_pos] = parseBalanced(command, index, '{', '}');
        words.emplace_back(ParsedWord{std::move(word_text), true});
        index = end_pos;
        continue;
      }
      if (command[index] == '"') {
        ++index;
        std::string word_text;
        bool escaped = false;
        while (index < command.size()) {
          const char ch = command[index++];
          if (escaped) {
            word_text += ch;
            escaped = false;
            continue;
          }
          if (ch == '\\') {
            escaped = true;
            continue;
          }
          if (ch == '"') {
            break;
          }
          word_text += ch;
        }
        words.emplace_back(ParsedWord{std::move(word_text), false});
        continue;
      }

      std::string word_text;
      int bracket_depth = 0;
      int brace_depth = 0;
      bool escaped = false;
      while (index < command.size()) {
        const char ch = command[index];
        if (escaped) {
          word_text += ch;
          escaped = false;
          ++index;
          continue;
        }
        if (ch == '\\') {
          escaped = true;
          ++index;
          continue;
        }
        if (bracket_depth == 0 && brace_depth == 0 && std::isspace(static_cast<unsigned char>(ch)) != 0) {
          break;
        }
        if (ch == '[') {
          ++bracket_depth;
        } else if (ch == ']' && bracket_depth > 0) {
          --bracket_depth;
        } else if (ch == '{') {
          ++brace_depth;
        } else if (ch == '}' && brace_depth > 0) {
          --brace_depth;
        }
        word_text += ch;
        ++index;
      }
      words.emplace_back(ParsedWord{std::move(word_text), false});
    }
    return words;
  }

  static auto parseBalanced(const std::string& text, std::size_t open_pos, char open_ch, char close_ch)
      -> std::pair<std::string, std::size_t>
  {
    std::string result;
    int depth = 0;
    bool escaped = false;
    for (std::size_t index = open_pos; index < text.size(); ++index) {
      const char ch = text[index];
      if (escaped) {
        result += ch;
        escaped = false;
        continue;
      }
      if (ch == '\\') {
        escaped = true;
        continue;
      }
      if (ch == open_ch) {
        if (depth > 0) {
          result += ch;
        }
        ++depth;
        continue;
      }
      if (ch == close_ch) {
        --depth;
        if (depth == 0) {
          return {result, index + 1U};
        }
        result += ch;
        continue;
      }
      result += ch;
    }
    return {result, text.size()};
  }

  static auto matchingBracketPos(const std::string& text, std::size_t open_pos) -> std::size_t
  {
    int bracket_depth = 0;
    int brace_depth = 0;
    bool in_quote = false;
    bool escaped = false;
    for (std::size_t index = open_pos; index < text.size(); ++index) {
      const char ch = text[index];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (ch == '\\') {
        escaped = true;
        continue;
      }
      if (ch == '"' && brace_depth == 0) {
        in_quote = !in_quote;
        continue;
      }
      if (!in_quote) {
        if (ch == '{') {
          ++brace_depth;
        } else if (ch == '}' && brace_depth > 0) {
          --brace_depth;
        } else if (ch == '[' && brace_depth == 0) {
          ++bracket_depth;
        } else if (ch == ']' && brace_depth == 0) {
          --bracket_depth;
          if (bracket_depth == 0) {
            return index;
          }
        }
      }
    }
    return std::string::npos;
  }

  auto substituteVariablesToString(const std::string& text) -> std::string
  {
    std::string result;
    for (std::size_t index = 0U; index < text.size();) {
      if (text[index] != '$') {
        result += text[index++];
        continue;
      }
      const auto [var_name, end_pos] = parseVariableName(text, index);
      if (var_name.empty()) {
        result += text[index++];
        continue;
      }
      if (const auto iter = _variables.find(var_name); iter != _variables.end()) {
        result += valueToString(iter->second);
      }
      index = end_pos;
    }
    return result;
  }

  static auto findInnermostBracket(const std::string& text) -> std::pair<std::size_t, std::size_t>
  {
    std::vector<std::size_t> open_positions;
    int brace_depth = 0;
    bool in_quote = false;
    bool escaped = false;
    for (std::size_t index = 0U; index < text.size(); ++index) {
      const char ch = text[index];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (ch == '\\') {
        escaped = true;
        continue;
      }
      if (ch == '"' && brace_depth == 0) {
        in_quote = !in_quote;
        continue;
      }
      if (in_quote) {
        continue;
      }
      if (ch == '{') {
        ++brace_depth;
        continue;
      }
      if (ch == '}' && brace_depth > 0) {
        --brace_depth;
        continue;
      }
      if (brace_depth != 0) {
        continue;
      }
      if (ch == '[') {
        open_positions.push_back(index);
      } else if (ch == ']' && !open_positions.empty()) {
        return {open_positions.back(), index};
      }
    }
    return {std::string::npos, std::string::npos};
  }

  static auto parseVariableName(const std::string& text, std::size_t dollar_pos) -> std::pair<std::string, std::size_t>
  {
    if (dollar_pos + 1U >= text.size()) {
      return {{}, dollar_pos + 1U};
    }
    if (text[dollar_pos + 1U] == '{') {
      const auto close_pos = text.find('}', dollar_pos + 2U);
      if (close_pos == std::string::npos) {
        return {{}, dollar_pos + 1U};
      }
      return {text.substr(dollar_pos + 2U, close_pos - dollar_pos - 2U), close_pos + 1U};
    }
    std::size_t pos = dollar_pos + 1U;
    while (pos < text.size()) {
      const auto ch = static_cast<unsigned char>(text[pos]);
      if (std::isalnum(ch) == 0 && text[pos] != '_') {
        break;
      }
      ++pos;
    }
    if (pos == dollar_pos + 1U) {
      return {{}, dollar_pos + 1U};
    }
    return {text.substr(dollar_pos + 1U, pos - dollar_pos - 1U), pos};
  }

  auto evaluateWord(const ParsedWord& word) -> SdcValue
  {
    if (word.braced) {
      return makeStringValue(word.text);
    }
    const auto clean_text = trim(word.text);
    if (!clean_text.empty() && clean_text.front() == '$') {
      const auto [var_name, end_pos] = parseVariableName(clean_text, 0U);
      if (!var_name.empty() && end_pos == clean_text.size()) {
        if (const auto iter = _variables.find(var_name); iter != _variables.end()) {
          return iter->second;
        }
        return {};
      }
    }
    if (clean_text.size() >= 2U && clean_text.front() == '[') {
      const auto close_pos = matchingBracketPos(clean_text, 0U);
      if (close_pos == clean_text.size() - 1U) {
        return evaluateCommandWithoutCommandSubstitution(expandBracketCommandsToString(clean_text.substr(1U, clean_text.size() - 2U)));
      }
    }

    const auto expanded = expandBracketCommandsToString(word.text);
    return makeStringValue(substituteVariablesToString(expanded));
  }

  auto evaluatePlainWord(const ParsedWord& word) -> SdcValue
  {
    if (word.braced) {
      return makeStringValue(word.text);
    }
    const auto clean_text = trim(word.text);
    if (!clean_text.empty() && clean_text.front() == '$') {
      const auto [var_name, end_pos] = parseVariableName(clean_text, 0U);
      if (!var_name.empty() && end_pos == clean_text.size()) {
        if (const auto iter = _variables.find(var_name); iter != _variables.end()) {
          return iter->second;
        }
        return {};
      }
    }
    return makeStringValue(substituteVariablesToString(word.text));
  }

  auto evaluatePlainWords(const std::vector<ParsedWord>& words, std::size_t start_index = 0U) -> std::vector<SdcValue>
  {
    std::vector<SdcValue> values;
    values.reserve(words.size() - std::min(start_index, words.size()));
    for (std::size_t index = start_index; index < words.size(); ++index) {
      values.push_back(evaluatePlainWord(words[index]));
    }
    return values;
  }

  auto expandBracketCommandsToString(std::string text) -> std::string
  {
    for (std::size_t iteration = 0U; iteration < kCommandSubstitutionLimit; ++iteration) {
      const auto [open_pos, close_pos] = findInnermostBracket(text);
      if (open_pos == std::string::npos || close_pos == std::string::npos) {
        return text;
      }
      const auto command = text.substr(open_pos + 1U, close_pos - open_pos - 1U);
      const auto command_value = evaluateCommandWithoutCommandSubstitution(command);
      text.replace(open_pos, close_pos - open_pos + 1U, valueToString(command_value));
    }
    _data.diagnostics.emplace_back("command_substitution_depth_limit");
    return text;
  }

  auto evaluateWords(const std::vector<ParsedWord>& words, std::size_t start_index = 0U) -> std::vector<SdcValue>
  {
    std::vector<SdcValue> values;
    values.reserve(words.size() - std::min(start_index, words.size()));
    for (std::size_t index = start_index; index < words.size(); ++index) {
      values.push_back(evaluateWord(words[index]));
    }
    return values;
  }

  auto evaluateCommand(const std::string& command) -> SdcValue
  {
    const auto words = parseWords(command);
    if (words.empty()) {
      return {};
    }
    const auto command_name = valueToString(evaluateWord(words.front()));
    if (command_name == "set") {
      return evaluateSet(words);
    }
    if (command_name == "expr") {
      return evaluateExpr(words);
    }
    if (command_name == "set_units") {
      evaluateSetUnits(evaluateWords(words, 1U));
      return {};
    }
    if (command_name == "get_ports") {
      return evaluateCollection(SdcObjectKind::kPort, evaluateWords(words, 1U));
    }
    if (command_name == "get_pins") {
      return evaluateCollection(SdcObjectKind::kPin, evaluateWords(words, 1U));
    }
    if (command_name == "get_nets") {
      return evaluateCollection(SdcObjectKind::kNet, evaluateWords(words, 1U));
    }
    if (command_name == "get_clocks") {
      return evaluateGetClocks(evaluateWords(words, 1U));
    }
    if (command_name == "all_clocks") {
      return evaluateAllClocks();
    }
    if (command_name == "create_clock") {
      return evaluateCreateClock(evaluateWords(words, 1U));
    }
    if (command_name == "create_generated_clock") {
      return evaluateCreateGeneratedClock(evaluateWords(words, 1U));
    }
    if (command_name == "set_case_analysis") {
      evaluateSetCaseAnalysis(evaluateWords(words, 1U));
      return {};
    }
    if (!command_name.empty()) {
      _data.diagnostics.emplace_back("ignored_sdc_command:" + command_name);
    }
    return {};
  }

  auto evaluateCommandWithoutCommandSubstitution(const std::string& command) -> SdcValue
  {
    const auto words = parseWords(command);
    if (words.empty()) {
      return {};
    }
    const auto command_name = valueToString(evaluatePlainWord(words.front()));
    if (command_name == "set") {
      return evaluateSetPlain(words);
    }
    if (command_name == "expr") {
      return evaluateExprPlain(words);
    }
    if (command_name == "set_units") {
      evaluateSetUnits(evaluatePlainWords(words, 1U));
      return {};
    }
    if (command_name == "get_ports") {
      return evaluateCollection(SdcObjectKind::kPort, evaluatePlainWords(words, 1U));
    }
    if (command_name == "get_pins") {
      return evaluateCollection(SdcObjectKind::kPin, evaluatePlainWords(words, 1U));
    }
    if (command_name == "get_nets") {
      return evaluateCollection(SdcObjectKind::kNet, evaluatePlainWords(words, 1U));
    }
    if (command_name == "get_clocks") {
      return evaluateGetClocks(evaluatePlainWords(words, 1U));
    }
    if (command_name == "all_clocks") {
      return evaluateAllClocks();
    }
    if (command_name == "create_clock") {
      return evaluateCreateClock(evaluatePlainWords(words, 1U));
    }
    if (command_name == "create_generated_clock") {
      return evaluateCreateGeneratedClock(evaluatePlainWords(words, 1U));
    }
    if (command_name == "set_case_analysis") {
      evaluateSetCaseAnalysis(evaluatePlainWords(words, 1U));
      return {};
    }
    if (!command_name.empty()) {
      _data.diagnostics.emplace_back("ignored_sdc_command:" + command_name);
    }
    return {};
  }

  auto evaluateSet(const std::vector<ParsedWord>& words) -> SdcValue
  {
    if (words.size() < 2U) {
      _data.diagnostics.emplace_back("ignored_set_without_variable");
      return {};
    }
    const auto variable_name = words[1].text;
    if (words.size() == 2U) {
      if (const auto iter = _variables.find(variable_name); iter != _variables.end()) {
        return iter->second;
      }
      return {};
    }
    if (words.size() == 3U) {
      auto value = evaluateWord(words[2]);
      _variables[variable_name] = value;
      return value;
    }
    auto values = evaluateWords(words, 2U);
    std::vector<std::string> joined_values;
    joined_values.reserve(values.size());
    std::ranges::transform(values, std::back_inserter(joined_values),
                           [](const SdcValue& value) -> std::string { return valueToString(value); });
    auto value = makeStringValue(joinStrings(joined_values));
    _variables[variable_name] = value;
    return value;
  }

  auto evaluateSetPlain(const std::vector<ParsedWord>& words) -> SdcValue
  {
    if (words.size() < 2U) {
      _data.diagnostics.emplace_back("ignored_set_without_variable");
      return {};
    }
    const auto variable_name = words[1].text;
    if (words.size() == 2U) {
      if (const auto iter = _variables.find(variable_name); iter != _variables.end()) {
        return iter->second;
      }
      return {};
    }
    if (words.size() == 3U) {
      auto value = evaluatePlainWord(words[2]);
      _variables[variable_name] = value;
      return value;
    }
    auto values = evaluatePlainWords(words, 2U);
    std::vector<std::string> joined_values;
    joined_values.reserve(values.size());
    std::ranges::transform(values, std::back_inserter(joined_values),
                           [](const SdcValue& value) -> std::string { return valueToString(value); });
    auto value = makeStringValue(joinStrings(joined_values));
    _variables[variable_name] = value;
    return value;
  }

  auto evaluateExpr(const std::vector<ParsedWord>& words) -> SdcValue
  {
    std::string expression;
    for (std::size_t index = 1U; index < words.size(); ++index) {
      if (!expression.empty()) {
        expression += ' ';
      }
      expression += words[index].braced ? substituteVariablesToString(words[index].text) : valueToString(evaluateWord(words[index]));
    }
    double expression_value = 0.0;
    if (!ArithmeticParser(expression).parse(expression_value)) {
      _data.diagnostics.emplace_back("unresolved_expr:" + expression);
      return makeStringValue("0");
    }
    std::ostringstream stream;
    stream << expression_value;
    return makeStringValue(stream.str());
  }

  auto evaluateExprPlain(const std::vector<ParsedWord>& words) -> SdcValue
  {
    std::string expression;
    for (std::size_t index = 1U; index < words.size(); ++index) {
      if (!expression.empty()) {
        expression += ' ';
      }
      expression += words[index].braced ? substituteVariablesToString(words[index].text) : valueToString(evaluatePlainWord(words[index]));
    }
    double expression_value = 0.0;
    if (!ArithmeticParser(expression).parse(expression_value)) {
      _data.diagnostics.emplace_back("unresolved_expr:" + expression);
      return makeStringValue("0");
    }
    std::ostringstream stream;
    stream << expression_value;
    return makeStringValue(stream.str());
  }

  auto evaluateSetUnits(const std::vector<SdcValue>& args) -> void
  {
    for (std::size_t index = 0U; index < args.size(); ++index) {
      if (valueToString(args[index]) == "-time" && index + 1U < args.size()) {
        _time_unit_ns = timeUnitToNs(valueToString(args[index + 1U]));
        ++index;
      }
    }
  }

  static auto evaluateCollection(SdcObjectKind kind, const std::vector<SdcValue>& args) -> SdcValue
  {
    std::vector<std::string> patterns;
    for (std::size_t index = 0U; index < args.size(); ++index) {
      const auto text = valueToString(args[index]);
      if (text.empty()) {
        continue;
      }
      if (isOption(text)) {
        if ((text == "-of_objects" || text == "-filter") && index + 1U < args.size()) {
          ++index;
        }
        continue;
      }
      for (const auto& item : splitListText(text)) {
        patterns.emplace_back(item);
      }
    }
    return makeObjectValue(kind, patterns);
  }

  auto evaluateGetClocks(const std::vector<SdcValue>& args) -> SdcValue
  {
    if (args.empty()) {
      return evaluateAllClocks();
    }
    return evaluateCollection(SdcObjectKind::kClock, args);
  }

  auto evaluateAllClocks() -> SdcValue
  {
    std::vector<std::string> clock_names;
    clock_names.reserve(_clock_period_by_name.size());
    for (const auto& [clock_name, period] : _clock_period_by_name) {
      (void) period;
      clock_names.emplace_back(clock_name);
    }
    return makeObjectValue(SdcObjectKind::kClock, clock_names);
  }

  auto evaluateCreateClock(const std::vector<SdcValue>& args) -> SdcValue
  {
    SdcClockDecl clock;
    clock.kind = SdcClockDecl::Kind::kPrimary;
    double period = 0.0;
    bool period_resolved = false;

    for (std::size_t index = 0U; index < args.size(); ++index) {
      const auto token = valueToString(args[index]);
      if (token == "-name" && index + 1U < args.size()) {
        clock.clock_name = valueToString(args[++index]);
      } else if (token == "-period" && index + 1U < args.size()) {
        period_resolved = parseDoubleValue(valueToString(args[++index]), period);
      } else if (token == "-waveform" && index + 1U < args.size()) {
        ++index;
      } else if (token == "-add") {
        continue;
      } else if (isOption(token)) {
        if (index + 1U < args.size()) {
          ++index;
        }
      } else {
        appendRefsFromValue(clock.targets, args[index], SdcObjectKind::kUnknown);
      }
    }

    clock.is_virtual = clock.targets.empty();
    if (clock.clock_name.empty() && !clock.targets.empty()) {
      clock.clock_name = clock.targets.front().pattern;
    }
    if (clock.clock_name.empty()) {
      _data.diagnostics.emplace_back("ignored_create_clock_without_name");
      return {};
    }
    clock.period_ns = period * _time_unit_ns;
    clock.period_resolved = period_resolved;
    _clock_period_by_name[clock.clock_name] = clock.period_ns;
    const auto clock_name = clock.clock_name;
    _data.clocks.push_back(std::move(clock));
    return makeObjectValue(SdcObjectKind::kClock, {clock_name});
  }

  auto evaluateCreateGeneratedClock(const std::vector<SdcValue>& args) -> SdcValue
  {
    SdcClockDecl clock;
    clock.kind = SdcClockDecl::Kind::kGenerated;
    for (std::size_t index = 0U; index < args.size(); ++index) {
      const auto token = valueToString(args[index]);
      if (token == "-name" && index + 1U < args.size()) {
        clock.clock_name = valueToString(args[++index]);
      } else if (token == "-source" && index + 1U < args.size()) {
        appendRefsFromValue(clock.generated_sources, args[++index], SdcObjectKind::kUnknown);
      } else if (token == "-master_clock" && index + 1U < args.size()) {
        clock.master_clock_name = valueToString(args[++index]);
      } else if (token == "-divide_by" && index + 1U < args.size()) {
        (void) parseIntValue(valueToString(args[++index]), clock.divide_by);
      } else if (token == "-multiply_by" && index + 1U < args.size()) {
        (void) parseIntValue(valueToString(args[++index]), clock.multiply_by);
      } else if (token == "-invert") {
        clock.invert = true;
      } else if ((token == "-edges" || token == "-edge_shift") && index + 1U < args.size()) {
        ++index;
      } else if (isOption(token)) {
        if (index + 1U < args.size()) {
          ++index;
        }
      } else {
        appendRefsFromValue(clock.targets, args[index], SdcObjectKind::kUnknown);
      }
    }

    clock.is_virtual = clock.targets.empty();
    if (clock.clock_name.empty()) {
      _data.diagnostics.emplace_back("ignored_generated_clock_without_name");
      return {};
    }

    auto lookup_name = clock.master_clock_name;
    if (lookup_name.empty() && !clock.generated_sources.empty()) {
      lookup_name = clock.generated_sources.front().pattern;
    }
    if (const auto iter = _clock_period_by_name.find(lookup_name); iter != _clock_period_by_name.end()) {
      clock.period_ns = iter->second;
      clock.period_resolved = true;
      if (clock.divide_by > 1) {
        clock.period_ns *= clock.divide_by;
      }
      if (clock.multiply_by > 1) {
        clock.period_ns /= clock.multiply_by;
      }
    }
    _clock_period_by_name[clock.clock_name] = clock.period_ns;
    const auto clock_name = clock.clock_name;
    _data.clocks.push_back(std::move(clock));
    return makeObjectValue(SdcObjectKind::kClock, {clock_name});
  }

  auto evaluateSetCaseAnalysis(const std::vector<SdcValue>& args) -> void
  {
    if (args.size() < 2U) {
      _data.diagnostics.emplace_back("ignored_set_case_analysis_without_object");
      return;
    }
    SdcCaseAnalysis analysis;
    (void) parseIntValue(valueToString(args.front()), analysis.value);
    for (std::size_t index = 1U; index < args.size(); ++index) {
      appendRefsFromValue(analysis.objects, args[index], SdcObjectKind::kUnknown);
    }
    _data.case_analyses.push_back(std::move(analysis));
  }

  double _time_unit_ns = 1.0;
  SdcClockData _data;
  std::unordered_map<std::string, SdcValue> _variables;
  std::map<std::string, double> _clock_period_by_name;
};

auto objectKindToSourcePrefix(SdcObjectKind kind) -> const char*
{
  switch (kind) {
    case SdcObjectKind::kPort:
      return "port:";
    case SdcObjectKind::kPin:
      return "pin:";
    case SdcObjectKind::kNet:
      return "net:";
    case SdcObjectKind::kClock:
      return "clock:";
    case SdcObjectKind::kUnknown:
      return "";
  }
  return "";
}

auto primarySourceExpression(const SdcClockDecl& clock) -> std::string
{
  const auto& refs = clock.targets.empty() ? clock.generated_sources : clock.targets;
  if (refs.empty()) {
    return {};
  }
  return std::string(objectKindToSourcePrefix(refs.front().kind)) + refs.front().pattern;
}

}  // namespace

SdcClockReader::SdcClockReader() : SdcClockReader(configuredSdcPath())
{
}

SdcClockReader::SdcClockReader(std::string sdc_path) : _sdc_path(std::move(sdc_path))
{
}

auto SdcClockReader::readClockData() const -> SdcClockData
{
  SdcClockData data;
  if (_sdc_path.empty()) {
    schema::EmitDiagnostic(schema::DiagnosticLevel::kWarning, "SdcClockReader", "SDC clock read skipped because SDC path is empty.",
                           {{"clock_source", "sdc"}});
    return data;
  }
  if (!std::filesystem::exists(_sdc_path)) {
    schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "SdcClockReader", "SDC clock read skipped because SDC file does not exist.",
                           {{"clock_source", "sdc"}, {"sdc_path", _sdc_path}});
    LOG_ERROR << "SdcClockReader: SDC file does not exist: " << _sdc_path;
    return data;
  }

  data = SdcSubsetEvaluator().readFile(_sdc_path);
  for (const auto& diagnostic : data.diagnostics) {
    if (diagnostic.starts_with("ignored_sdc_command:")) {
      continue;
    }
    schema::EmitDiagnostic(schema::DiagnosticLevel::kWarning, "SdcClockReader", "SDC clock subset parser diagnostic.",
                           {{"detail", diagnostic}});
  }
  LOG_INFO << "SdcClockReader: parsed " << data.clocks.size() << " clock declaration(s) and " << data.case_analyses.size()
           << " case-analysis record(s) from " << _sdc_path;
  return data;
}

auto SdcClockReader::readDeclarationsOnly() const -> std::vector<std::tuple<std::string, std::string, double, bool>>
{
  std::vector<std::tuple<std::string, std::string, double, bool>> declarations;
  const auto data = readClockData();
  declarations.reserve(data.clocks.size());
  for (const auto& clock : data.clocks) {
    declarations.emplace_back(clock.clock_name, primarySourceExpression(clock), clock.period_ns, clock.period_resolved);
  }
  return declarations;
}

}  // namespace icts
