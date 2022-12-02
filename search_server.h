#pragma once

#include <string>
#include <vector>
#include <set>
#include <tuple>
#include <map>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <execution>
#include <deque>
#include <cmath>
#include <future>
#include "string_processing.h"
#include "document.h"
#include "read_input_functions.h"
#include "log_duration.h"
#include "concurrent_map.h"

const double MAX_DIFFERENCE = 1e-6;
using match_type = std::tuple<std::vector<std::string_view>, DocumentStatus>;

class SearchServer {
public:
    
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words)
        : stop_words_(MakeUniqueNonEmptyStrings(stop_words))  // Extract non-empty stop words
    {
        if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
            throw std::invalid_argument("Some of stop words are invalid");
        }
    }

    explicit SearchServer(std::string_view stop_words_text)
        : SearchServer(SplitIntoWords(stop_words_text))  // Invoke delegating constructor
        // from string container
    {
    }
    
    explicit SearchServer(std::string c_string)
        : SearchServer(SplitIntoWords(c_string))
        {}

    void AddDocument(int document_id, std::string_view document, DocumentStatus status, const std::vector<int>& ratings);

    void RemoveDocument(int document_id);
    void RemoveDocument(std::execution::parallel_policy policy, int document_id);
    void RemoveDocument(std::execution::sequenced_policy policy, int document_id);

    
    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const;
    template <typename ExecutionPolicy, typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(ExecutionPolicy policy, std::string_view raw_query, DocumentPredicate document_predicate) const;
    
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentStatus status) const;
    template<typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy policy, std::string_view raw_query, DocumentStatus status) const;
    
    std::vector<Document> FindTopDocuments(std::string_view raw_query) const;
    template<typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy policy, std::string_view raw_query) const;
    
    int GetDocumentCount() const;

    match_type MatchDocument(std::string_view raw_query, int document_id) const;
    match_type MatchDocument(std::execution::sequenced_policy policy, std::string_view raw_query, int document_id) const;
    match_type MatchDocument(std::execution::parallel_policy policy, std::string_view raw_query, int document_id) const;
    
    std::set<int>::const_iterator begin();
    std::set<int>::const_iterator end();

    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
        std::string text;
    };


    const std::set<std::string, std::less<>> stop_words_;
    std::map<int, std::map<std::string_view, double>> word_frequencies_;
    std::map<std::string_view, std::map<int, double>> word_to_document_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> document_ids_;

    bool IsStopWord(std::string_view word) const;
    static bool IsValidWord(std::string_view word);
    std::vector<std::string_view> SplitIntoWordsNoStop( std::string_view text) const;
    static int ComputeAverageRating(const std::vector<int>& ratings);

    struct QueryWord {
        std::string_view data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(std::string_view text) const;

    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };
    

    Query ParseQuery(std::string_view text) const;
    double ComputeWordInverseDocumentFreq(std::string_view word) const;
    
    template <typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(const Query& query, DocumentPredicate document_predicate) const;
    template <typename ExecutionPolicy, typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(ExecutionPolicy policy, const Query& query, DocumentPredicate document_predicate) const;
};


template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const {
    auto query = ParseQuery(raw_query);
    query.plus_words.erase(std::unique(query.plus_words.begin(), query.plus_words.end()), query.plus_words.end());
    query.minus_words.erase(std::unique(query.minus_words.begin(), query.minus_words.end()), query.minus_words.end());
    
    auto matched_documents = FindAllDocuments(query, document_predicate);

    std::sort(matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
        if (std::abs(lhs.relevance - rhs.relevance) < ::MAX_DIFFERENCE) {
            return lhs.rating > rhs.rating;
        }
        else {
            return lhs.relevance > rhs.relevance;
        }
        });
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}
    

template <typename ExecutionPolicy, typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy policy, std::string_view raw_query, DocumentPredicate document_predicate) const {
    if constexpr(std::is_same_v<std::decay_t<ExecutionPolicy>, std::execution::sequenced_policy>) {
        return SearchServer::FindTopDocuments(raw_query, document_predicate);
    }
    auto query = ParseQuery(raw_query);
    
    query.plus_words.erase(std::unique(query.plus_words.begin(), query.plus_words.end()), query.plus_words.end());
    query.minus_words.erase(std::unique(query.minus_words.begin(), query.minus_words.end()), query.minus_words.end());
    
    auto matched_documents = FindAllDocuments(policy, query, document_predicate);

    std::sort(std::execution::par, matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
        if (std::abs(lhs.relevance - rhs.relevance) < ::MAX_DIFFERENCE) {
            return lhs.rating > rhs.rating;
        }
        else {
            return lhs.relevance > rhs.relevance;
        }
        });
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}

template<typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy policy, std::string_view raw_query, DocumentStatus status) const {
    return SearchServer::FindTopDocuments(policy, raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
        });
}

template<typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy policy, std::string_view raw_query) const {
    return SearchServer::FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}


template <typename ExecutionPolicy, typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(ExecutionPolicy policy, const Query& query, DocumentPredicate document_predicate) const {
    if constexpr(std::is_same_v<std::decay_t<ExecutionPolicy>, std::execution::sequenced_policy>) {
        return SearchServer::FindAllDocuments(query, document_predicate);
    }
    
    ConcurrentMap<int, double> document_to_relevance(15);
    for_each(
        policy,
        query.plus_words.begin(),
        query.plus_words.end(),
        [&] (std::string_view word) {
            
            if(word_to_document_freqs_.count(word) == 0) {
                return;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (auto [document_id, freq]: word_to_document_freqs_.at(word)) {
                const auto& document_data = documents_.at(document_id);
                if (document_predicate(document_id, document_data.status, document_data.rating)) {
                    document_to_relevance[document_id].ref_to_value += static_cast<double>(freq * inverse_document_freq);
                }
            }
        }
    );
    
    for_each(
    policy, 
    query.minus_words.begin(),
    query.minus_words.end(),
    [&](std::string_view word) {
        for (const int document_id : document_ids_) {
            if (word_frequencies_.at(document_id).count(word) == 0) {
                continue;
            }
            document_to_relevance.erase(document_id);
        }

    });
    
    auto temp = document_to_relevance.BuildOrdinaryMap();
    std::vector<Document> matched_documents;
    for (const auto [document_id, relevance] : temp) {
        matched_documents.push_back({ document_id, relevance, documents_.at(document_id).rating });
    }
    return matched_documents;
}


template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(const Query& query, DocumentPredicate document_predicate) const {
    
    std::map<int, double> document_to_relevance;
    for (std::string_view word: query.plus_words) {
        if(word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
        
        for (auto [document_id, freq]: word_to_document_freqs_.at(word)) {
            const auto& document_data = documents_.at(document_id);
            if (document_predicate(document_id, document_data.status, document_data.rating)) {
                document_to_relevance[document_id] += freq * inverse_document_freq;
            }
        }
    }
    
    for (const int document_id : document_ids_) {
        for (std::string_view word : query.minus_words) {
            if (word_frequencies_.at(document_id).count(word) == 0) {
                continue;
            }
            document_to_relevance.erase(document_id);
        }
    }
    std::vector<Document> matched_documents;
    for (const auto [document_id, relevance] : document_to_relevance) {
        matched_documents.push_back({ document_id, relevance, documents_.at(document_id).rating });
    }
    return matched_documents;
}

void AddDocument(SearchServer& search_server, int document_id, std::string_view document, DocumentStatus status,
    const std::vector<int>& ratings);

void FindTopDocuments(const SearchServer& search_server, std::string_view raw_query);

void MatchDocuments(const SearchServer& search_server, std::string_view query);
void RemoveDuplicates(SearchServer& search_server);
