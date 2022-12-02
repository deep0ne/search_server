#include "search_server.h"
#include <memory>
#include <list>
using namespace std;

void SearchServer::AddDocument(int document_id, string_view document, DocumentStatus status, const vector<int>& ratings) {
    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw invalid_argument("Invalid document_id"s);
    }
    documents_.emplace(document_id, DocumentData{ ComputeAverageRating(ratings), status, std::string(document)});
    
    const auto words = SplitIntoWordsNoStop(documents_.at(document_id).text);
    const double inv_word_count = 1.0 / words.size();
    if (!words.empty()) {
        for (string_view word : words) {
            word_frequencies_[document_id][word] += inv_word_count;
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
    } else {
        word_frequencies_[document_id] = {};
    }
    
    document_ids_.insert(document_id);
        
}

void SearchServer::RemoveDocument(int document_id) {
    if (document_ids_.count(document_id) == 0) {
        return;
    }
    documents_.erase(document_id);
    document_ids_.erase(document_id);

    for (auto& [word, _] : word_frequencies_.at(document_id)) {
        if (word_to_document_freqs_.count(word)) {
            word_to_document_freqs_[word].erase(document_id);
        }
    }
    
    word_frequencies_.erase(document_id);

}

void SearchServer::RemoveDocument(execution::sequenced_policy policy, int document_id) {
    RemoveDocument(document_id);
}

void SearchServer::RemoveDocument(execution::parallel_policy policy, int document_id) {
    if(!document_ids_.count(document_id)) {
        return;
    }
    documents_.erase(document_id);
    document_ids_.erase(document_id);
    
    vector<pair<const string_view, double>*> v(word_frequencies_.at(document_id).size());
    transform(
        policy,
        word_frequencies_.at(document_id).begin(),
        word_frequencies_.at(document_id).end(),
        v.begin(),
        [&](pair<const string_view, double>& temp) {return &temp;}
    );
    
    for_each(
        execution::par,
        v.begin(),
        v.end(),
        [&](const pair<const string_view, double>* temp) {
            word_to_document_freqs_[temp->first].erase(document_id);
        }
    );
    word_frequencies_.erase(document_id);
}


int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

const map<string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {
    static const map<string_view, double> empty;
    if (word_frequencies_.count(document_id)) {
        return word_frequencies_.at(document_id);
    }
    return empty;
}

vector<Document> SearchServer::FindTopDocuments(string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
        });
}

vector<Document> SearchServer::FindTopDocuments(string_view raw_query) const {
    return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(string_view raw_query, int document_id) const {
    const auto query = ParseQuery(raw_query);

    vector<string_view> matched_words;

    for (string_view word : query.plus_words) {
        
        if (word_frequencies_.at(document_id).count(word) == 0) {
            continue;
        }
        
        if(word_frequencies_.at(document_id).count(word)) {
            matched_words.push_back(word);
        }
    }
    for (string_view word : query.minus_words) {
        if(word_frequencies_.at(document_id).count(word) == 0) {
            continue;
        }
        if (word_frequencies_.at(document_id).count(word)) {
            matched_words.clear();
        }
    }
    return { matched_words, documents_.at(document_id).status };
}

tuple<vector<string_view>, DocumentStatus> 
SearchServer::MatchDocument(execution::sequenced_policy policy, string_view raw_query, int document_id) const {
    return MatchDocument(raw_query, document_id);
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(execution::parallel_policy policy, string_view raw_query, int document_id) const {
    const auto query = ParseQuery(raw_query);
    const map<string_view, double>& tmp = word_frequencies_.at(document_id);
    
    bool flag = any_of(
        policy,
        query.minus_words.begin(),
        query.minus_words.end(),
        [&](string_view view) {return tmp.count(view);}
    );
    if (flag) {
        return { {}, documents_.at(document_id).status};
    }
    
    if(query.plus_words.empty()) {
        return { {}, documents_.at(document_id).status};
    }
    
    vector<string_view> matched_words;
    
    copy_if(
        query.plus_words.begin(),
        query.plus_words.end(),
        back_inserter(matched_words),
        [&](string_view view) {return tmp.count(view);}
    );
    sort( matched_words.begin(), matched_words.end() );
    matched_words.erase( unique( matched_words.begin(), matched_words.end() ), matched_words.end() );
    
    return { matched_words, documents_.at(document_id).status};
    
}

bool SearchServer::IsStopWord(string_view word) const {
    return stop_words_.count(word) > 0;
}

bool SearchServer::IsValidWord(string_view word) {
    // A valid word must not contain special characters
    return none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
        });
}

vector<string_view> SearchServer::SplitIntoWordsNoStop(string_view text) const {
    vector<string_view> words;
    for (string_view word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw invalid_argument("Word "s + std::string(word) + " is invalid"s);
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(const vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }

    return accumulate(ratings.begin(), ratings.end(), 0) / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(string_view text) const {
    if (text.empty()) {
        throw invalid_argument("Query word is empty"s);
    }
    string_view word = text;
    bool is_minus = false;
    if (word[0] == '-') {
        is_minus = true;
        word = word.substr(1);
    }
    if (word.empty() || word[0] == '-' || !IsValidWord(word)) {
        throw invalid_argument("Query word "s + std::string(text) + " is invalid");
    }

    return { word, is_minus, IsStopWord(word) };
}

SearchServer::Query SearchServer::ParseQuery(string_view text) const {
    Query result;
    for (string_view word : SplitIntoWords(text)) {
        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            }
            else {
                result.plus_words.push_back(query_word.data);
            }
        }
    }
    sort(result.plus_words.begin(), result.plus_words.end());
    sort(result.minus_words.begin(), result.minus_words.end());
    return result;
}


double SearchServer::ComputeWordInverseDocumentFreq(string_view word) const {

    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}

void AddDocument(SearchServer& search_server, int document_id, string_view document, DocumentStatus status,
    const vector<int>& ratings) {
    try {
        search_server.AddDocument(document_id, document, status, ratings);
    }
    catch (const invalid_argument& e) {
        cout << "Ошибка добавления документа "s << document_id << ": "s << e.what() << endl;
    }
}

void FindTopDocuments(const SearchServer& search_server, string_view raw_query) {
    LOG_DURATION_STREAM("Operation time", cout);
    cout << "Результаты поиска по запросу: "s << std::string(raw_query) << endl;
    try {
        for (const Document& document : search_server.FindTopDocuments(raw_query)) {
            PrintDocument(document);
        }
    }
    catch (const invalid_argument& e) {
        cout << "Ошибка поиска: "s << e.what() << endl;
    }
}

set<int>::const_iterator SearchServer::begin() {
    return document_ids_.begin();
}

set<int>::const_iterator SearchServer::end() {
    return document_ids_.end();
}

void MatchDocuments(SearchServer& search_server, string_view query) {
    LOG_DURATION_STREAM("Operation time", cout);
    try {
        cout << "Матчинг документов по запросу: " << query << endl;
        for (auto document_id : search_server) {
            const auto [words, status] = search_server.MatchDocument(query, document_id);
            PrintMatchDocumentResult(document_id, words, status);
        }
    }
    catch (const invalid_argument& e) {
        cout << "Ошибка матчинга документов на запрос "s << query << ": "s << e.what() << endl;
    }
}
