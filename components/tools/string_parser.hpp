#pragma once
#include <memory>
#include <vector>
#include <string>

class StringParser {
  public:
    std::pair<std::string, std::string::size_type> static GetNextWord(std::string            str,
                                                                      std::string::size_type from_pos = 0)
    {
        auto char_it = str.begin() + from_pos;
        if (char_it >= str.end())
            return { "", -1 };

        std::string word;

        while (char_it < str.end()) {
            if (isalnum(*char_it))
                break;
            else
                char_it++;
        }

        if (char_it >= str.end())
            return { "", -1 };

        while (char_it < str.end()) {
            if (isalnum(*char_it)) {
                word += *char_it;
                char_it++;
            }
            else
                break;
        }

        return { word, char_it - str.begin() };
    }
    std::vector<std::string> static GetWords(std::string str)
    {
        std::vector<std::string> string_list;

        auto [new_word, next_pos] = GetNextWord(str);

        if (new_word == "")
            return string_list;

        string_list.push_back(new_word);

        while (true) {
            std::string word;
            std::tie(word, next_pos) = GetNextWord(str, next_pos);

            string_list.push_back(std::move(word));

            if (next_pos >= str.size() or next_pos == -1)
                return string_list;
        }
    }


};