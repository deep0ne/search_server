#include "process_queries.h"
#include <execution>
#include <algorithm>

using namespace std;

vector<vector<Document>> ProcessQueries(
    const SearchServer& search_server,
    const vector<string>& queries) {
    vector<vector<Document>> answer(queries.size());
    transform(
        execution::par,
        queries.begin(), queries.end(),
        answer.begin(),
        [&search_server](const string& query) {return search_server.FindTopDocuments(query);}
    );
    return answer;
}

vector<Document> ProcessQueriesJoined(
    const SearchServer& search_server,
    const std::vector<std::string>& queries) {
    
    vector<vector<Document>> process = ProcessQueries(search_server, queries);
    auto num_chars = transform_reduce(
      execution::par,
      process.begin(),  
      process.end(),  
      size_t{0},
      plus<>{},
      [](vector<Document>& m) {return m.size();}
    ); 
    
    vector<Document> answer(num_chars);
    auto it = answer.begin();
    for (vector<Document> document: process) {
        transform(
            execution::par,
            document.begin(), document.end(),
            it,
            [](Document doc) {return doc;}
        );
        it += document.size();
    }
    return answer;
    
}
