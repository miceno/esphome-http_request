#pragma once

#include "http_request.h"

#ifdef USE_ARDUINO

#if defined(USE_ESP32) || defined(USE_RP2040)
#include <HTTPClient.h>
#endif
#ifdef USE_ESP8266
// #include <ESP8266HTTPClient.h>
#include <AsyncHTTPRequest_Generic.hpp>
#ifdef USE_HTTP_REQUEST_ESP8266_HTTPS
#include <WiFiClientSecure.h>
#error "HTTPS is currently unsupported"
#endif
#endif

namespace esphome {
namespace http_request_async {

#include <string>

using std::string;


class AsyncHttpContainer {
private:
  const char* TAG{ "AHC" };

public:
  typedef std::function<void(void*, AsyncHttpContainer*, const string&)> responseCallbackType;
  typedef std::function<void(void*, AsyncHttpContainer*)> errorCallbackType;

  AsyncHttpContainer(const string& url,
                     const string& method,
                     const string& body,
                     const string& requestParams)
    : _url(url), _method(method), _request_body(body), _request_params(requestParams), _response_code(0) {
  }
  void addRequestHeader(const string& name, const string& value){
      _request.setReqHeader(name.c_str(), value.c_str());
  }
  bool start();

  void setRequestDefaultHeaders();

  void onResponseCallback(responseCallbackType callback, void* arg = 0){
    _responseCallback = callback;
    _responseCallbackArg = arg;
  }
  void onErrorCallback(errorCallbackType callback, void* arg = 0){
    _errorCallback = callback;
    _errorCallbackArg = arg;
  }

  string get_url(){ return _url; }
  void set_url(const string &s){ _url = s; }

  string get_method(){ return _method; }
  void set_method(const string &s){ _method = s; }

  string get_request_params(){ return _request_params; }
  void set_request_params(const string &s){ _request_params = s; }

  const char* get_response_body(){ return _response_body.c_str(); }
  void set_response_body(const string &s){ _response_body = s; }

  int get_response_code(){ return _request.responseHTTPcode(); }
  int get_response_length(){ return _request.responseLength(); }
  uint32_t get_elapsed_time(){ return _request.elapsedTime(); }


  string get_request_body(){ return _request_body; }
  void set_request_body(const string &s){ _request_body = s; }

  void setAvailableData(const string& body);
  void setAvailableData(const char *body);

  void dataNotAvailable(){ _isDataAvailable = false; }
  bool isDataAvailable(){ return _isDataAvailable; }

  void requestNotSent(){ _isRequestSent = false; }
  bool isRequestSent(){ return _isRequestSent; }

  void raise_error(void){
    ESP_LOGD(TAG, "Raise error");
    if(_errorCallback){
      _errorCallback((void*)_errorCallbackArg, this);
    }
  }

private:
  string _url;
  string _method;

  AsyncHTTPRequest _request;

  string _request_params;
  string _response_body;
  string _request_body;
  int _response_code;

  bool _isDataAvailable{ false };
  bool _isRequestSent{ false };

  responseCallbackType _responseCallback;
  void* _responseCallbackArg{ nullptr };

  errorCallbackType _errorCallback;
  void* _errorCallbackArg{ nullptr };
};


class HttpRequestArduino;
class HttpContainerArduino : public HttpContainer {
 public:
  int read(uint8_t *buf, size_t max_len) override;
  void end() override;

 protected:
  friend class HttpRequestArduino;
  HTTPClient client_{};
};

class HttpRequestArduino : public HttpRequestComponent {
 public:
  std::shared_ptr<HttpContainer> start(std::string url, std::string method, std::string body,
                                       std::list<Header> headers) override;
};

}  // namespace http_request
}  // namespace esphome

#endif  // USE_ARDUINO
