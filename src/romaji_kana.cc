#include "romaji_kana.h"

namespace {

constexpr std::pair<const char *, const char *> kRules[] = {
  // 母音
  {"a","あ"},{"i","い"},{"u","う"},{"e","え"},{"o","お"},
  // か行
  {"ka","か"},{"ki","き"},{"ku","く"},{"ke","け"},{"ko","こ"},
  {"kya","きゃ"},{"kyi","きぃ"},{"kyu","きゅ"},{"kye","きぇ"},{"kyo","きょ"},
  // さ行
  {"sa","さ"},{"si","し"},{"su","す"},{"se","せ"},{"so","そ"},
  {"shi","し"},
  {"sha","しゃ"},{"shu","しゅ"},{"she","しぇ"},{"sho","しょ"},
  {"sya","しゃ"},{"syi","しぃ"},{"syu","しゅ"},{"sye","しぇ"},{"syo","しょ"},
  // た行
  {"ta","た"},{"ti","ち"},{"tu","つ"},{"te","て"},{"to","と"},
  {"chi","ち"},{"tsu","つ"},
  {"cha","ちゃ"},{"chu","ちゅ"},{"che","ちぇ"},{"cho","ちょ"},
  {"tya","ちゃ"},{"tyi","ちぃ"},{"tyu","ちゅ"},{"tye","ちぇ"},{"tyo","ちょ"},
  {"thi","てぃ"},
  // な行
  {"na","な"},{"ni","に"},{"nu","ぬ"},{"ne","ね"},{"no","の"},
  {"nn","ん"},
  {"nya","にゃ"},{"nyi","にぃ"},{"nyu","にゅ"},{"nye","にぇ"},{"nyo","にょ"},
  // は行
  {"ha","は"},{"hi","ひ"},{"fu","ふ"},{"hu","ふ"},{"he","へ"},{"ho","ほ"},
  {"hya","ひゃ"},{"hyi","ひぃ"},{"hyu","ひゅ"},{"hye","ひぇ"},{"hyo","ひょ"},
  {"fa","ふぁ"},{"fi","ふぃ"},{"fe","ふぇ"},{"fo","ふぉ"},
  // ま行
  {"ma","ま"},{"mi","み"},{"mu","む"},{"me","め"},{"mo","も"},
  {"mya","みゃ"},{"myi","みぃ"},{"myu","みゅ"},{"mye","みぇ"},{"myo","みょ"},
  // や行
  {"ya","や"},{"yi","い"},{"yu","ゆ"},{"ye","いぇ"},{"yo","よ"},
  // ら行
  {"ra","ら"},{"ri","り"},{"ru","る"},{"re","れ"},{"ro","ろ"},
  {"rya","りゃ"},{"ryi","りぃ"},{"ryu","りゅ"},{"rye","りぇ"},{"ryo","りょ"},
  // わ行
  {"wa","わ"},{"wi","うぃ"},{"wu","う"},{"we","うぇ"},{"wo","を"},
  // が行
  {"ga","が"},{"gi","ぎ"},{"gu","ぐ"},{"ge","げ"},{"go","ご"},
  {"gya","ぎゃ"},{"gyi","ぎぃ"},{"gyu","ぎゅ"},{"gye","ぎぇ"},{"gyo","ぎょ"},
  // ざ行
  {"za","ざ"},{"zi","じ"},{"zu","ず"},{"ze","ぜ"},{"zo","ぞ"},
  {"ji","じ"},
  {"ja","じゃ"},{"ju","じゅ"},{"je","じぇ"},{"jo","じょ"},
  {"zya","じゃ"},{"zyi","じぃ"},{"zyu","じゅ"},{"zye","じぇ"},{"zyo","じょ"},
  // だ行
  {"da","だ"},{"di","ぢ"},{"du","づ"},{"de","で"},{"do","ど"},
  {"dya","ぢゃ"},{"dyi","ぢぃ"},{"dyu","ぢゅ"},{"dye","ぢぇ"},{"dyo","ぢょ"},
  {"dhi","でぃ"},
  // ば行
  {"ba","ば"},{"bi","び"},{"bu","ぶ"},{"be","べ"},{"bo","ぼ"},
  {"bya","びゃ"},{"byi","びぃ"},{"byu","びゅ"},{"bye","びぇ"},{"byo","びょ"},
  // ぱ行
  {"pa","ぱ"},{"pi","ぴ"},{"pu","ぷ"},{"pe","ぺ"},{"po","ぽ"},
  {"pya","ぴゃ"},{"pyi","ぴぃ"},{"pyu","ぴゅ"},{"pye","ぴぇ"},{"pyo","ぴょ"},
  // 小文字 x系
  {"xa","ぁ"},{"xi","ぃ"},{"xu","ぅ"},{"xe","ぇ"},{"xo","ぉ"},
  {"xya","ゃ"},{"xyu","ゅ"},{"xyo","ょ"},
  {"xtu","っ"},{"xtsu","っ"},{"xwa","ゎ"},
  // 小文字 l系
  {"la","ぁ"},{"li","ぃ"},{"lu","ぅ"},{"le","ぇ"},{"lo","ぉ"},
  {"lya","ゃ"},{"lyu","ゅ"},{"lyo","ょ"},
  {"ltu","っ"},{"ltsu","っ"},
  // ん（直接入力）
  {"n'","ん"},
};

} // namespace

RomajiKana::RomajiKana() {
  for (const auto &[rom, kana] : kRules) {
    table_[rom] = kana;
    // 各キーの strict prefix をすべて登録
    std::string prefix;
    for (char c : std::string_view(rom).substr(0, std::string_view(rom).size() - 1)) {
      prefix += c;
      prefixes_.insert(prefix);
    }
  }
}

std::string RomajiKana::feed(char c) {
  pending_ += c;
  return tryConvert();
}

void RomajiKana::backspace() {
  if (!pending_.empty()) {
    pending_.pop_back();
  }
}

std::string RomajiKana::tryConvert() {
  std::string result;
  while (!pending_.empty()) {
    // 完全一致
    if (auto it = table_.find(pending_); it != table_.end()) {
      result += it->second;
      pending_.clear();
      break;
    }
    // プレフィックス一致 → 続きを待つ
    if (prefixes_.count(pending_)) break;

    // n + 子音（a/i/u/e/o/n/y/apostrophe 以外）→ ん確定
    if (pending_[0] == 'n' && pending_.size() >= 2) {
      const char next = pending_[1];
      if (next != 'a' && next != 'i' && next != 'u' && next != 'e' &&
          next != 'o' && next != 'n' && next != 'y' && next != '\'') {
        result += "ん";
        pending_.erase(0, 1);
        continue;
      }
    }

    // 促音: 同じ子音の連続（n 以外）→ っ
    if (pending_.size() >= 2 &&
        pending_[0] == pending_[1] &&
        pending_[0] != 'n') {
      result += "っ";
      pending_.erase(0, 1);
      continue;
    }

    // どのルールにも一致しない → 先頭1文字をそのまま出力
    result += pending_[0];
    pending_.erase(0, 1);
  }
  return result;
}
