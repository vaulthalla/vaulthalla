#pragma once

#include <istream>
#include <string>
#include <unordered_set>
#include <vector>

namespace vh::crypto::password {

class Strength {
  public:
    static unsigned short passwordStrengthCheck(const std::string& password);
    // True when the password is a dictionary word once case and leading/trailing digits and symbols are stripped
    // ("Sunshine2024!"). Words merely appearing inside a longer password don't count: a random 64-character password
    // almost always contains some three-letter word.
    static bool isDictionaryWord(const std::string& password);
    static bool isCommonWeakPassword(const std::string& password);
    static bool isPwnedPassword(const std::string& password);
    static void loadDictionaryFromURL(const std::string& url);
    static void loadDictionary(std::istream& words);
    static void loadCommonWeakPasswordsFromURLs(const std::vector<std::string>& urls);
    static void loadCommonWeakPasswords(std::istream& passwords);
    static std::string SHA1Hex(const std::string& input);

  private:
    static std::unordered_set<std::string> dictionaryWords_;
    static std::unordered_set<std::string> commonWeakPasswords_;

    static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* s);
    static std::string downloadURL(const std::string& url);
};

}
