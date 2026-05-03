#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

class RomajiKana {
public:
  RomajiKana();

  // 文字を追加し、確定したひらがなを返す（未確定は pending() で取得）
  std::string feed(char c);

  // pending の末尾1文字を削除
  void backspace();

  const std::string &pending() const { return pending_; }
  void reset() { pending_.clear(); }

private:
  std::string pending_;
  std::unordered_map<std::string, std::string> table_;
  std::unordered_set<std::string> prefixes_;

  std::string tryConvert();
};
