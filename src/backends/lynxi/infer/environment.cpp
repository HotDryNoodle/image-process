#include "environment.h"
#include <iostream>

Environment::Environment() {}

Environment::~Environment() { 
  destroy(); 
}

void Environment::createContext(uint32_t deviceId, std::function<void(lynStream_t, ErrorMsg &&)> cb) {
  std::lock_guard<std::mutex> l(m_mtx);
  if (m_mapContext.end() == m_mapContext.find(deviceId)) {
    lynsdk::Context* pContext = new Context(deviceId);
    pContext->on_stream_error(cb);

    m_mapContext[deviceId] = pContext;
  }
}

void Environment::setContext(uint32_t deviceId) {
  std::lock_guard<std::mutex> l(m_mtx);
  std::map<uint32_t, lynsdk::Context*>::iterator iter = m_mapContext.find(deviceId);
  if (m_mapContext.end() != iter) {
    iter->second->set_current();
  }
}

void Environment::destroyContext(uint32_t deviceId) {
  std::lock_guard<std::mutex> l(m_mtx);
  std::map<uint32_t, lynsdk::Context*>::iterator iter = m_mapContext.find(deviceId);
  if (m_mapContext.end() != iter) {
    delete iter->second;
    m_mapContext.erase(iter);
  }
}

void Environment::destroy() {
  std::lock_guard<std::mutex> l(m_mtx);
  std::map<uint32_t, lynsdk::Context*>::iterator iter;
  for(iter=m_mapContext.begin(); iter!=m_mapContext.end();)
  {
      delete iter->second;
      m_mapContext.erase(iter++);
  }
}
