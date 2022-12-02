#include "string_processing.h"

using namespace std;

vector<string_view> SplitIntoWords(string_view str) {
    vector<string_view> result;
    int64_t not_space = str.find_first_not_of(' ');
    if(not_space == -1) {
        return result;
    }
    str.remove_prefix(not_space);
    const int64_t pos_end = str.npos;
    
    while (!str.empty()) {
        int64_t space = str.find(' ');
        if (space == 0) {
            str.remove_prefix(space+1);
        } else {
            result.push_back(space == pos_end ? str.substr(0, pos_end) : str.substr(0, space));
            str.remove_prefix(min(str.size(), str.find_first_of(' ')));
        }
    }

    return result;
}
