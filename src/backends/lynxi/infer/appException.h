/**
 *@file appException.h
 *@author lynxi
 *@version v1.0
 *@date 2022-09-13
 *@par Copyright:
 *© 2022 北京灵汐科技有限公司 版权所有。
 * 注意：以下内容均为北京灵汐科技有限公司原创，未经本公司允许，不得转载，否则将视为侵权；对于不遵守此声明或者其他违法使用以下内容者，本公司依法保留追究权。\n
 *© 2022 Lynxi Technologies Co., Ltd. All rights reserved.
 * NOTICE: All information contained here is, and remains the property of Lynxi.
 *This file can not be copied or distributed without the permission of Lynxi
 *Technologies Co., Ltd.
 *@brief app异常定义
 */

#ifndef __APP_EXCEPTION_H_
#define __APP_EXCEPTION_H_

#include <sstream>
#include <string>

class AppException : public std::exception {
   private:
    std::string file;
    int line;
    std::string detail;  // 详细信息
    std::string s;
    void String() {
        std::stringstream ss;
        ss << "[" << file << ":" << line << "]";
        if (!detail.empty()) {
            ss << " detail: " << detail;
        }
        ss << std::endl;
        s = ss.str();
    }

   public:
    AppException(const std::string& file, int line, const std::string& detail = "")
        : file(file), line(line), detail(detail) {
        String();
    }
    AppException(const AppException& e) noexcept { *this = e; };
    const char* what() const noexcept override { return s.c_str(); }
};

// 抛出异常，并打印信息
#define THROW_ERR(detail)                         \
    do {                                          \
        auto _detail = (detail);                  \
        throw AppException{__FILE__, __LINE__, _detail}; \
    } while (0)

#endif
