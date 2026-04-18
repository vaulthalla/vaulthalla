#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace vh::crypto::password {

class Strength {
  public:
    static unsigned short passwordStrengthCheck(const std::string& password);
    static bool containsDictionaryWord(const std::string& password);
    static bool isCommonWeakPassword(const std::string& password);
    static bool isPwnedPassword(const std::string& password);
    static void loadDictionaryFromURL(const std::string& url);
    static void loadCommonWeakPasswordsFromURLs(const std::vector<std::string>& urls);
    static std::string SHA1Hex(const std::string& input);

    static std::string escape_uri_component(const std::string& input);

  private:
    static std::unordered_set<std::string> dictionaryWords_;
    static std::unordered_set<std::string> commonWeakPasswords_;

    static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* s);
    static std::string downloadURL(const std::string& url);
};

}
