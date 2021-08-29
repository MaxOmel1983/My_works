#include <iostream>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std;

constexpr int MAX_RESULT_DOCUMENT_COUNT = 5;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result;
    cin >> result;
    ReadLine();
    return result;
}

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        }
        else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }
    return words;
}

struct Document {
    Document() = default;

    Document(int id, double relevance, int rating)
        : id(id), relevance(relevance), rating(rating)
    {
    }

    int id = 0;
    double relevance = 0.0;
    int rating = 0;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

template<typename StringContainer>
set<string> MakeUniqueNonEmptyStrings(const StringContainer& strings) {
    set<string> non_empty_strings;
    for (const string& str : strings) {
        if (!str.empty()) {
            non_empty_strings.insert(str);
        }
    }
    return non_empty_strings;
}

class SearchServer {
public:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words)
        : stop_words_(MakeUniqueNonEmptyStrings(stop_words)) {

        for (const string& word : stop_words) {
            if (!IsValidWord(word)) {
                throw invalid_argument("Some of stop words are invalid"s);
            }
        }
    }

    //template <typename StringContainer>
    //explicit SearchServer(const StringContainer& stop_words)
    //    : stop_words_(MakeUniqueNonEmptyStrings(stop_words))  // Extract non-empty stop words
    //{
    //    if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
    //        throw invalid_argument("Some of stop words are invalid"s);
    //    }
    //}

    explicit SearchServer(const string& stop_words_text)
        : SearchServer(SplitIntoWords(stop_words_text)) {
    }

    void AddDocument(int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {
        if ((document_id < 0) || (documents_.count(document_id) > 0)) {
            throw invalid_argument("Invalid document ID"s);
        }

        const vector<string> words = SplitIntoWordsNoStop(document);
        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id, DocumentData{ ComputeAverageRating(ratings), status });
        document_ids_.push_back(document_id);
    }

    size_t GetDocumentCount() const {
        return documents_.size();
    }

    int GetDocumentId(int index) const {
        return document_ids_.at(index);
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query, int document_id) const {
        const Query query = ParseQuery(raw_query);

        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        return { matched_words, documents_.at(document_id).status };
    }

    vector<Document> FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }

    vector<Document> FindTopDocuments(const string& raw_query, DocumentStatus status) const {
        return FindTopDocuments(raw_query, [c = status](int document_id, DocumentStatus status, int rating) {return status == c; });
    }

    template <typename DocumentPredicate>
    vector<Document> FindTopDocuments(const string& raw_query, DocumentPredicate document_predicate) const {
        const Query query = ParseQuery(raw_query);

        auto matched_documents = FindAllDocuments(query, document_predicate);

        sort(matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
            if (abs(lhs.relevance - rhs.relevance) < 1e-6) {
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

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;
    vector<int> document_ids_;

    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        int rating_sum = 0;
        for (const int rating : ratings) {
            rating_sum += rating;
        }
        return rating_sum / static_cast<int>(ratings.size());
    }

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    static bool IsValidWord(const string& word) {
        return none_of(word.begin(), word.end(), [](char c) {
            return c >= '\0' && c < ' ';
            });
    }

    vector<string> SplitIntoWordsNoStop(const string& text) const {
        if (!IsValidWord(text)) {
            throw invalid_argument("one of several words contains forbidden symbols: "s);
        }

        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }

        return words;
    }

    QueryWord ParseQueryWord(const string& text) const {
        string word = text;
        bool is_minus = false;
        if (word[0] == '-') {
            is_minus = true;
            word = word.substr(1);
        }
        if (word.empty() || word[0] == '-' || !IsValidWord(word)) {
            throw invalid_argument("Query word "s + text + " is invalid"s);
        }

        return { word, is_minus, IsStopWord(word) };
    }

    Query ParseQuery(const string& text) const {
        if (text.empty()) {
            throw invalid_argument("Query is empty"s);
        }

        Query query;
        for (const string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                }
                else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }

    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }

    template<typename DocumentPredicat>
    vector<Document> FindAllDocuments(const Query& query, DocumentPredicat document_predicat) const {
        map<int, double> document_to_relevance;

        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);

            for (const auto elem : word_to_document_freqs_.at(word)) {

                if (document_predicat(elem.first, documents_.at(elem.first).status, documents_.at(elem.first).rating)) {

                    document_to_relevance[elem.first] += elem.second * inverse_document_freq;
                }
            }
        }

        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto& elem : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(elem.first);
            }
        }

        vector<Document> matched_documents;
        for (const auto& elem : document_to_relevance) {
            matched_documents.push_back({ elem.first, elem.second, documents_.at(elem.first).rating });
        }
        return matched_documents;
    }
};



void PrintDocument(const Document& document) {
    cout << "{ "s
        << "document_id = "s << document.id << ", "s
        << "relevance = "s << document.relevance << ", "s
        << "rating = "s << document.rating
        << " }"s << endl;
}


int main() {
    // throw есть; необходимо добавит catch
    setlocale(LC_ALL, "RUS");

    try {
        // 210710
        // РАБОТА БЕЗ ОШИБОК В ДОКУМЕНТАХ ИЛИ ЗАПРОСЕ
       /* SearchServer search_server("и в на"s);

        search_server.AddDocument(0, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, { 8, -3 });
        search_server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, { 7, 2, 7 });
        search_server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, { 5, -12, 2, 1 });
        search_server.AddDocument(3, "белый кот пушистый хвост"s, DocumentStatus::ACTUAL, { 8, -3 });
        search_server.AddDocument(4, "ухоженный скворец евгений"s, DocumentStatus::BANNED, { 9 });

        cout << "ACTUAL by default:"s << endl;
        for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный кот"s)) {
            PrintDocument(document);
        }

        cout << "BANNED:"s << endl;
        for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный кот"s, DocumentStatus::BANNED)) {
            PrintDocument(document);
        }

        cout << "Even ids:"s << endl;
        for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный кот"s, [](int document_id, DocumentStatus status, int rating) { return document_id % 2 == 0; })) {
            PrintDocument(document);
        }*/
        // ИТОГ ПРОВЕРКИ: все работает, как ожидалось


        // РАБОТА С ЗАПЛАНИРОВННЫМИ ОШИБКАМИ В ИНИЦИАЛИЗАЦИИ КЛАССА И ДОКУМЕНТАХ
        //    - неправильные стоп-слова
        //    - неверный id документа
        //    - недопустимые символы

        // поочередно запускаем каждую ошибку и смотрим на результат         

        //// SearchServer search_server("и в н\x12а"s);                                                                      // проверка на "левые" символы в стоп-словах  // 210710 15_50 выбрасывает сообщение об ошибке в стоп-словах
        //SearchServer search_server("и в на"s);                                                                               // норм

        //// потребуются как "нормальные" документы, так и ошибочные        

        // search_server.AddDocument(0, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, { 8, -3 });                     // норм
        // search_server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, { 7, 2, 7 });                  // норм
        //// search_server.AddDocument(1, "ухоженный пёс пушистый хвост"s, DocumentStatus::ACTUAL, { 7, 2, 7 });         // проверка на повтор id                         // 210710 15_53 выбрасывает сообщение об ошибке в id документа
        // search_server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, { 5, -12, 2, 1 });       // норм
        //// search_server.AddDocument(-2, "ухоженный скворе и модный ошейник"s, DocumentStatus::ACTUAL, { 8, -3 });     // проверка на отрицательный id                  // 210710 15_55 выбрасывает сообщение об ошибке в id документа
        //search_server.AddDocument(4, "белый кот скво\x12рец хвост"s, DocumentStatus::ACTUAL, { 8, -3 });            // проверка на "левые" символы в документе         // 210710 15_56 выбрасывает сообщение о наличии неверных символов
        //// search_server.AddDocument(3, "ухоженный скворец евгений"s, DocumentStatus::BANNED, { 9 });                          // норм
        //
        //// запрос без ошибок
        //cout << "ACTUAL by default:"s << endl;
        //for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный кот"s)) {
        //    PrintDocument(document);
        //}

        //cout << "BANNED:"s << endl;
        //for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный кот"s, DocumentStatus::BANNED)) {
        //    PrintDocument(document);
        //}

        //cout << "Even ids:"s << endl;
        //for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный кот"s, [](int document_id, DocumentStatus status, int rating) { return document_id % 2 == 0; })) {
        //    PrintDocument(document);
        //}
        // ИТОГ ПРОВЕРКИ: при возникновении ОДНОЙ из нескольких ошибок все работает,как ожидалось; при наличии нескольких ошибок сообщается о той, которая возникает раньше


        // РАБОТА С ЗАПЛАНИРОВННЫМИ ОШИБКАМИ В ЗАПРОСАХ
        //    - неверный текст запроса
        //    - отсутствие запроса

        // поочередно запускаем каждую ошибку и смотрим на результат

        // без ошибок
        SearchServer search_server("и в на"s);

        search_server.AddDocument(0, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, { 8, -3 });
        search_server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, { 7, 2, 7 });
        search_server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, { 5, -12, 2, 1 });
        search_server.AddDocument(3, "белый кот пушистый хвост"s, DocumentStatus::ACTUAL, { 8, -3 });
        search_server.AddDocument(4, "ухоженный скворец евгений"s, DocumentStatus::BANNED, { 9 });

        // проверочный запрос запрос
           // 210710
        //cout << "ACTUAL by default:"s << endl;
        //for (const Document& document : search_server.FindTopDocuments(""s)) {                                  // проверка на empty                       // выводит сообщение о пустом запросе
        //    PrintDocument(document);
        //}

        //cout << "ACTUAL by default:"s << endl;
        //for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный --кот"s)) {          // проверка на --перед словом               // выводит сообщение о ошибке в слове запроса
        //    PrintDocument(document);
        //}

        //cout << "ACTUAL by default:"s << endl;
        //for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный - кот"s)) {          // проверка на - слово                      // выводит сообщение о ошибке в слове запроса
        //    PrintDocument(document);
        //}

        //cout << "ACTUAL by default:"s << endl;
        //for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный - "s)) {             // проверка на -                            // выводит сообщение о ошибке в слове запроса
        //    PrintDocument(document);
        //}

        cout << "ACTUAL by default:"s << endl;
        for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный -- "s)) {            // проверка на --                            // выводит сообщение о ошибке в слове запроса
            PrintDocument(document);
        }

        cout << "ACTUAL by default:"s << endl;
        for (const Document& document : search_server.FindTopDocuments("пушистый ухоженный -кот "s)) {          // проверка на -слово                        // исключает документы, содержащиние минус-слово
            PrintDocument(document);
        }

        cout << "ACTUAL by default:"s << endl;
        for (const Document& document : search_server.FindTopDocuments("пушистый скво\x12рец кот"s)) {          // проверка на "левые" символы                 // выводит сообщение о неверном слове в запросе
            PrintDocument(document);
        }
    }
    catch (const exception& e) {
        // изменить содержание фразы "Неверный формат ввода стоп-слов: "
        cout << e.what() << endl;
    }


    return 0;
}