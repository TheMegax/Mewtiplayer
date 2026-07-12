#pragma once
#include <string>
#include <vector>

namespace ParaboxAPI {

template<typename T>
struct Array {
    T* data = nullptr;
    size_t length = 0;
    void (*free_func)(T*) = nullptr;

    Array() = default;
    Array(T* d, size_t s, void(*f)(T*)) : data(d), length(s), free_func(f) {}
    
    ~Array() { 
        if (free_func && data) {
            free_func(data); 
        }
    }

    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    Array(Array&& other) noexcept : data(other.data), length(other.length), free_func(other.free_func) {
        other.data = nullptr;
        other.length = 0;
        other.free_func = nullptr;
    }

    Array& operator=(Array&& other) noexcept {
        if (this != &other) {
            if (free_func && data) free_func(data);
            data = other.data;
            length = other.length;
            free_func = other.free_func;
            other.data = nullptr;
            other.length = 0;
            other.free_func = nullptr;
        }
        return *this;
    }

    T* begin() const { return data; }
    T* end() const { return data + length; }
    size_t size() const { return length; }
    bool empty() const { return length == 0; }
    T& operator[](size_t i) const { return data[i]; }

    // Helper to extract back to std::vector inside the consuming DLL
    std::vector<T> to_vector() const {
        if (!data || length == 0) return std::vector<T>();
        return std::vector<T>(data, data + length);
    }
};

struct String {
    char* data = nullptr;
    size_t length = 0;
    void (*free_func)(char*) = nullptr;

    String() = default;
    String(char* d, size_t s, void(*f)(char*)) : data(d), length(s), free_func(f) {}
    
    ~String() { 
        if (free_func && data) free_func(data); 
    }

    String(const String&) = delete;
    String& operator=(const String&) = delete;

    String(String&& other) noexcept : data(other.data), length(other.length), free_func(other.free_func) {
        other.data = nullptr;
        other.length = 0;
        other.free_func = nullptr;
    }

    String& operator=(String&& other) noexcept {
        if (this != &other) {
            if (free_func && data) free_func(data);
            data = other.data;
            length = other.length;
            free_func = other.free_func;
            other.data = nullptr;
            other.length = 0;
            other.free_func = nullptr;
        }
        return *this;
    }

    const char* c_str() const { return data ? data : ""; }
    size_t size() const { return length; }
    bool empty() const { return length == 0; }
    
    // Helper to extract back to std::string inside the consuming DLL
    std::string to_string() const {
        if (!data || length == 0) return std::string();
        return std::string(data, length);
    }
};

// Helper macro to quickly allocate and return an array from a std::vector inside ParaboxAPI
template<typename T>
Array<T> MakeArray(const std::vector<T>& vec) {
    if (vec.empty()) return Array<T>();
    T* arr = new T[vec.size()];
    for (size_t i = 0; i < vec.size(); ++i) arr[i] = vec[i];
    return Array<T>(arr, vec.size(), [](T* p) { delete[] p; });
}

template<typename T>
Array<T> MakeArray(std::vector<T>& vec) {
    if (vec.empty()) return Array<T>();
    T* arr = new T[vec.size()];
    for (size_t i = 0; i < vec.size(); ++i) arr[i] = std::move(vec[i]);
    return Array<T>(arr, vec.size(), [](T* p) { delete[] p; });
}

template<typename T>
Array<T> MakeArray(std::vector<T>&& vec) {
    if (vec.empty()) return Array<T>();
    T* arr = new T[vec.size()];
    for (size_t i = 0; i < vec.size(); ++i) arr[i] = std::move(vec[i]);
    return Array<T>(arr, vec.size(), [](T* p) { delete[] p; });
}

// Helper to quickly return a String from a std::string inside ParaboxAPI
inline String MakeString(const std::string& str) {
    if (str.empty()) return String();
    char* arr = new char[str.size() + 1];
    for (size_t i = 0; i < str.size(); ++i) arr[i] = str[i];
    arr[str.size()] = '\0';
    return String(arr, str.size(), [](char* p) { delete[] p; });
}

} // namespace ParaboxAPI
