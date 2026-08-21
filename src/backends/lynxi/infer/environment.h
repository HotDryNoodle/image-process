/**
 *@file environment.h
 *@author lynxi
 *@version v1.0
 *@date 2022-08-26
 *@par Copyright:
 *© 2022 北京灵汐科技有限公司 版权所有。
 * 注意：以下内容均为北京灵汐科技有限公司原创，未经本公司允许，不得转载，否则将视为侵权；对于不遵守此声明或者其他违法使用以下内容者，本公司依法保留追究权。\n
 *© 2022 Lynxi Technologies Co., Ltd. All rights reserved.
 * NOTICE: All information contained here is, and remains the property of Lynxi.
 *This file can not be copied or distributed without the permission of Lynxi
 *Technologies Co., Ltd.
 *@brief app运行上下文
 */

#ifndef __ENVIRONMENT_H_
#define __ENVIRONMENT_H_

#include <sdk/context.hpp>
#include <sdk/error.hpp>
#include <map>
#include <mutex>

using namespace lynsdk;

/********************************************************************************
类名 : Environment(应用运行环境类)
Description: 单例模式，提供了应用运行所需的Context创建，设置和销毁的接口。lynsdk的部分
      接口依赖于Context，在调用这些接口前先调用Environment类的setContext
*******************************************************************************/
class Environment {
public:
  static Environment *GetInstance() {
    static Environment instance;
    return &instance;
  }

  ~Environment();

  /**
   * @brief 创建Context
   *
   * @param[in] deviceId 芯片id
   * @return 无
   */
  void createContext(uint32_t deviceId, std::function<void(lynStream_t, ErrorMsg &&)> cb);

   /**
   * @brief 设置Context，在调用lynsdk里依赖于Context的部分接口前调用
   *
   * @param[in] 无
   * @return 无
   */
  void setContext(uint32_t deviceId);
  
   /**
   * @brief 销毁Context
   *
   * @param[in] 无
   * @return 无
   */
  void destroyContext(uint32_t deviceId);

   /**
   * @brief 销毁所有Context
   *
   * @param[in] 无
   * @return 无
   */
  void destroy();

private:
  Environment();

  std::mutex m_mtx;
  std::map<uint32_t, Context*> m_mapContext;
};

#endif
