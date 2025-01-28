#include "http_request_arduino.h"
#include <AsyncHTTPRequest_Generic.h>

#ifdef USE_ARDUINO

#include "esphome/components/network/util.h"
#include "esphome/components/watchdog/watchdog.h"

#include "esphome/core/application.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"

namespace esphome {
namespace http_request_async {

static const char *const TAG = "http_request_async.arduino";

std::shared_ptr<HttpContainer> HttpRequestArduino::start(std::string url, std::string method, std::string body,
                                                         std::list<Header> headers) {
  if (!network::is_connected()) {
    this->status_momentary_error("failed", 1000);
    ESP_LOGW(TAG, "HTTP Request failed; Not connected to network");
    return nullptr;
  }

  std::shared_ptr<HttpContainerArduino> container = std::make_shared<HttpContainerArduino>();
  container->set_parent(this);

  const uint32_t start = millis();

  bool secure = url.find("https:") != std::string::npos;
  container->set_secure(secure);

  watchdog::WatchdogManager wdm(this->get_watchdog_timeout());

  if (this->follow_redirects_) {
    container->client_.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    container->client_.setRedirectLimit(this->redirect_limit_);
  } else {
    container->client_.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  }

#if defined(USE_ESP8266)
  std::unique_ptr<WiFiClient> stream_ptr;
#ifdef USE_HTTP_REQUEST_ESP8266_HTTPS
  if (secure) {
    ESP_LOGV(TAG, "ESP8266 HTTPS connection with WiFiClientSecure");
    stream_ptr = std::make_unique<WiFiClientSecure>();
    WiFiClientSecure *secure_client = static_cast<WiFiClientSecure *>(stream_ptr.get());
    secure_client->setBufferSizes(512, 512);
    secure_client->setInsecure();
  } else {
    stream_ptr = std::make_unique<WiFiClient>();
  }
#else
  ESP_LOGV(TAG, "ESP8266 HTTP connection with WiFiClient");
  if (secure) {
    ESP_LOGE(TAG, "Can't use HTTPS connection with esp8266_disable_ssl_support");
    return nullptr;
  }
  stream_ptr = std::make_unique<WiFiClient>();
#endif  // USE_HTTP_REQUEST_ESP8266_HTTPS

#if USE_ARDUINO_VERSION_CODE >= VERSION_CODE(3, 1, 0)  // && USE_ARDUINO_VERSION_CODE < VERSION_CODE(?, ?, ?)
  if (!secure) {
    ESP_LOGW(TAG, "Using HTTP on Arduino version >= 3.1 is **very** slow. Consider setting framework version to 3.0.2 "
                  "in your YAML, or use HTTPS");
  }
#endif  // USE_ARDUINO_VERSION_CODE
  bool status = container->client_.begin(*stream_ptr, url.c_str());

#elif defined(USE_RP2040)
  if (secure) {
    container->client_.setInsecure();
  }
  bool status = container->client_.begin(url.c_str());
#elif defined(USE_ESP32)
  bool status = container->client_.begin(url.c_str());
#endif

  App.feed_wdt();

  if (!status) {
    ESP_LOGW(TAG, "HTTP Request failed; URL: %s", url.c_str());
    container->end();
    this->status_momentary_error("failed", 1000);
    return nullptr;
  }

  container->client_.setReuse(true);
  container->client_.setTimeout(this->timeout_);
#if defined(USE_ESP32)
  container->client_.setConnectTimeout(this->timeout_);
#endif

  if (this->useragent_ != nullptr) {
    container->client_.setUserAgent(this->useragent_);
  }
  for (const auto &header : headers) {
    container->client_.addHeader(header.name, header.value, false, true);
  }

  // returned needed headers must be collected before the requests
  static const char *header_keys[] = {"Content-Length", "Content-Type"};
  static const size_t HEADER_COUNT = sizeof(header_keys) / sizeof(header_keys[0]);
  container->client_.collectHeaders(header_keys, HEADER_COUNT);

  App.feed_wdt();
  container->status_code = container->client_.sendRequest(method.c_str(), body.c_str());
  App.feed_wdt();
  if (container->status_code < 0) {
    ESP_LOGW(TAG, "HTTP Request failed; URL: %s; Error: %s", url.c_str(),
             HTTPClient::errorToString(container->status_code).c_str());
    this->status_momentary_error("failed", 1000);
    container->end();
    return nullptr;
  }

  if (!is_success(container->status_code)) {
    ESP_LOGE(TAG, "HTTP Request failed; URL: %s; Code: %d", url.c_str(), container->status_code);
    this->status_momentary_error("failed", 1000);
    // Still return the container, so it can be used to get the status code and error message
  }

  int content_length = container->client_.getSize();
  ESP_LOGD(TAG, "New Content-Length: %d", content_length);
  container->content_length = (size_t) content_length;
  container->duration_ms = millis() - start;

  return container;
}

int HttpContainerArduino::read(uint8_t *buf, size_t max_len) {
  const uint32_t start = millis();
  watchdog::WatchdogManager wdm(this->parent_->get_watchdog_timeout());

  WiFiClient *stream_ptr = this->client_.getStreamPtr();
  if (stream_ptr == nullptr) {
    ESP_LOGE(TAG, "Stream pointer vanished!");
    return -1;
  }

  int available_data = stream_ptr->available();
  int bufsize = std::min(max_len, std::min(this->content_length - this->bytes_read_, (size_t) available_data));

  if (bufsize == 0) {
    this->duration_ms += (millis() - start);
    return 0;
  }

  App.feed_wdt();
  int read_len = stream_ptr->readBytes(buf, bufsize);
  this->bytes_read_ += read_len;

  this->duration_ms += (millis() - start);

  return read_len;
}

void HttpContainerArduino::end() {
  watchdog::WatchdogManager wdm(this->parent_->get_watchdog_timeout());
  this->client_.end();
}

//
//
//
// new async http container
//
//
//


/*
  Set body data and flags.
*/
void AsyncHttpContainer::setAvailableData(const char *body) {
  _response_body = body;
  _isDataAvailable = true;
  _isRequestSent = true;

  if (_responseCallback) {
    _responseCallback(_responseCallbackArg, this, body);
  }
}

void AsyncHttpContainer::setAvailableData(const string &body) {
  setAvailableData(body.c_str());
}

/*
  Override this member to add your custom headers to the request.
*/
void AsyncHttpContainer::setRequestDefaultHeaders() {
}

void _handleRequestState(void* optParm, AsyncHTTPRequest* request, int readyState) {
  AsyncHttpContainer* httpContainer = (AsyncHttpContainer*)optParm;

  httpContainer->dataNotAvailable();
  ESP_LOGV(TAG, F("API State received=%d, request=%d"), readyState, request->readyState());

  if (readyState == readyStateDone) {
    ESP_LOGV(TAG, F("API Response Code = |%s|"), request->responseHTTPString().c_str());
    int responseCode = 0;
    httpContainer->requestNotSent();
    responseCode = httpContainer->get_response_code();
    if (responseCode > 0) {
      // TODO: Allow processing of response headers
      httpContainer->setAvailableData(request->responseText().c_str());

      ESP_LOGV(TAG, "HTTP response: %d\n**************************************", responseCode);
      ESP_LOGV(TAG, httpContainer->get_response_body());
      ESP_LOGV(TAG, "**************************************");

    } else {
      httpContainer->set_response_body("");
      ESP_LOGE(TAG, F("HTTP error: %d"), responseCode);
      httpContainer->raise_error();
    }
  }
}


bool AsyncHttpContainer::start() {
  ESP_LOGV(TAG, "* %s %s, %s", _method.c_str(), _url.c_str(), _request_body.c_str());

  _isRequestSent = false;
  _isDataAvailable = false;
  _response_body.clear();
  _request.onReadyStateChange(_handleRequestState, (void*)this);
  _request.setDebug(true);

  bool requestOpenResult = false;
  requestOpenResult = _request.open(_method.c_str(), _url.c_str());
  setRequestDefaultHeaders();

  if (requestOpenResult) {
    // Send request and terminate, the callback will get the response.
    _isRequestSent = _request.send(_request_body.c_str());
  } else {
    ESP_LOGE(TAG, "Can't send bad request");
    raise_error();
  }
  return requestOpenResult && _isRequestSent;
}





}  // namespace http_request
}  // namespace esphome

#endif  // USE_ARDUINO
