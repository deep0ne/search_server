#pragma once

#include <string>
#include <vector>
#include <deque>
#include "search_server.h"
#include "document.h"

class RequestQueue {
public:
    explicit RequestQueue(const SearchServer& search_server)
        : search_server_(search_server)
    {
    }

    template <typename DocumentPredicate>
    std::vector<Document> AddFindRequest(std::string_view raw_query, DocumentPredicate document_predicate) {
        return search_server_.FindTopDocuments(raw_query, document_predicate);
    }

    std::vector<Document> AddFindRequest(std::string_view raw_query, DocumentStatus status);

    std::vector<Document> AddFindRequest(std::string_view raw_query);

    int GetNoResultRequests() const;

private:
    struct QueryResult {
        int is_empty;
        std::vector<Document> docs;
    };

    std::deque<QueryResult> requests_;
    const static int min_in_day_ = 1440;
    const SearchServer& search_server_;
    int empty_requests_ = 0;
    int seconds_ = 0;
};
