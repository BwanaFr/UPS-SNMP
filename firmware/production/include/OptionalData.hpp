#ifndef _OPTIONAL_DATA_HPP__
#define _OPTIONAL_DATA_HPP__
#include <cstdint>

template <typename T>
class OptionalData{
public:
    OptionalData(T value) : set_(true), value_(value) {
    };
    OptionalData() : set_(false){}

    virtual ~OptionalData() =  default;
    inline void setValue(T value){
        value_ = value;
        set_ = true;
    }

    operator bool() const {
        return set_;
    }

    inline T getValue() const {
        return value_;
    }

    inline void reset(){
        set_ = false;
    }

    T& operator =(const T& val){
        setValue(val);
        return value_;
    }

private:
    bool set_;
    T value_;
};

#endif
