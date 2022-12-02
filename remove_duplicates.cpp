#include "remove_duplicates.h"

using namespace std;

/* document_ids_ - приватное поле класса SearchServer
   RemoveDuplicates не является методом класса
   как обратиться к этому полю? сделать метод для получения document_ids_?
   Или сделать removeduplicates методом класса?
*/

void RemoveDuplicates(SearchServer& search_server) {
    vector<int> ids(search_server.begin(), search_server.end()); // решил так

    set<set<string>> m;
    for (const int document_id : ids) {
        set<string> test;
        auto word_freq = search_server.GetWordFrequencies(document_id);
        for (auto& [word, freq] : word_freq) {
            test.insert(word);
        }

        if (!m.count(test)) {
            m.insert(test);
        }
        else {
            cout << "Found duplicate document id " << document_id << endl;
            search_server.RemoveDocument(document_id);
        }
    }
}

