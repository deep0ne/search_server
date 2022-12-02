#include "request_queue.h"

using namespace std;

vector<Document> RequestQueue::AddFindRequest(string_view raw_query, DocumentStatus status) {
    return search_server_.FindTopDocuments(raw_query, status);
}

vector<Document> RequestQueue::AddFindRequest(string_view raw_query) {
    seconds_++;
    if (seconds_ > min_in_day_) {
        if (requests_.front().is_empty == 1) {
            empty_requests_--;
        }
        requests_.pop_front();
    }
    vector<Document> vec = search_server_.FindTopDocuments(raw_query);

    if (vec.empty()) {
        empty_requests_++;
        QueryResult query = { 1, vec };
        requests_.push_back(query);
    }
    else {
        QueryResult query = { 0, vec };
        requests_.push_back(query);
    }

    return vec;
}

int RequestQueue::GetNoResultRequests() const {
    return empty_requests_;
}
