/*
* Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program; if not, write to the Free Software
*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*
*/
#ifndef DBGWADAPTER_H_
#define DBGWADAPTER_H_

#include "DBGWMock.h"

namespace DBGW3
{

  // dbgw 1.0 / 2.0 common error
  const static int DBGWCONNECTOR_CREATE_FAILED         = -22901;
  const static int DBGWCONNECTOR_NOMORE_FETCH          = -22902;
  const static int DBGWCONNECTOR_NOT_EXIST             = -22903;
  const static int DBGWCONNECTOR_NOT_PROPER_OP         = -22904;
  const static int DBGWCONNECTOR_TIMEOUT               = -22905;
  const static int DBGWCONNECTOR_WRONG_TRANSACTIONID   = -22906;
  const static int DBGWCONNECTOR_INVALID_TRANSACTIONID = -22907;
  const static int DBGWCONNECTOR_NXAPI_ERROR           = -22908;
  const static int DBGWCONNECTOR_TOO_MANY_WAITS        = -22909;

  const static int DBGWCONNECTOR_EXEC_FAILED           = -22910;
  const static int DBGWCONNECTOR_INVALID_PROTOCOL      = -22911;
  const static int DBGWCONNECTOR_ALREAY_IN_TRANSACTION = -22912;
  const static int DBGWCONNECTOR_NOT_IN_TRANSACTION    = -22913;
  const static int DBGWCONNECTOR_SERVER_TIMEOUT        = -22914;

  // dbgw 1.0 / 2.0 extension error
  const static int DBGWCONNECTOR_SUCCESS               = 0;
  const static int DBGWCONNECTOR_FAILED                = -1;
  const static int DBGWCONNECTOR_INVALID_HANDLE        = -22915;
  const static int DBGWCONNECTOR_NOT_ENOUGH_MEMORY     = -22916;
  const static int DBGWCONNECTOR_INVALID_PARAMETER     = -22917;
  const static int DBGWCONNECTOR_CALL_NOT_IMPLEMENTED  = -22918;
  const static int DBGWCONNECTOR_BUFFER_OVERFLOW       = -22919;

  namespace XPlatform
  {

    const static int XSUCCESS                          = DBGWCONNECTOR_SUCCESS;
    const static int XFAILED                           = DBGWCONNECTOR_FAILED;
    const static int XINVALID_HANDLE                   = DBGWCONNECTOR_INVALID_HANDLE;
    const static int XNOT_ENOUGH_MEMORY                = DBGWCONNECTOR_NOT_ENOUGH_MEMORY;
    const static int XINVALID_PARAMETER                = DBGWCONNECTOR_INVALID_PARAMETER;
    const static int XCALL_NOT_IMPLEMENTED             = DBGWCONNECTOR_CALL_NOT_IMPLEMENTED;
    const static int XBUFFER_OVERFLOW                  = DBGWCONNECTOR_BUFFER_OVERFLOW;

  }

  const static int MRSC_SUCCESS                        = DBGWCONNECTOR_SUCCESS;

  const static int MRSC_INVALID_PARAMETER              = DBGWCONNECTOR_INVALID_PARAMETER;
  const static int MRSC_OUTOFRANGE                     = DBGWCONNECTOR_FAILED;
  const static int MRSC_INVALID_BUFSIZE                = DBGWCONNECTOR_BUFFER_OVERFLOW;
  const static int MRSC_NOT_ENOUGH_MEMORY              = DBGWCONNECTOR_NOT_ENOUGH_MEMORY;

  const static int MRSC_LOADCONFIG_FAIL                = DBGWCONNECTOR_CREATE_FAILED;
  const static int MRSC_LOADSERVERADDRESS_FAIL         = DBGWCONNECTOR_CREATE_FAILED;
  const static int MRSC_CONNECTNETWORK_FAIL            = DBGWCONNECTOR_CREATE_FAILED;
  const static int MRSC_HANDSHAKE_FAIL                 = DBGWCONNECTOR_CREATE_FAILED;
  const static int MRSC_SERVER_FULL                    = DBGWCONNECTOR_CREATE_FAILED;
  const static int MRSC_NO_SHARED_ENV                  = DBGWCONNECTOR_CREATE_FAILED;

  const static int MRSC_INVALID_ADDRESSFORMAT          = DBGWCONNECTOR_INVALID_PARAMETER;

  const static int MRSC_CANNOT_FIND_SOCKET             = DBGWCONNECTOR_INVALID_PARAMETER;
  const static int MRSC_ALREADY_EXIST_ADDRESS          = DBGWCONNECTOR_INVALID_PARAMETER;
  const static int MRSC_ALREADY_JOINED                 = DBGWCONNECTOR_INVALID_PARAMETER;
  const static int MRSC_NOT_EXIST_ADDRESS              = DBGWCONNECTOR_INVALID_PARAMETER;
  const static int MRSC_NOT_JOINED                     = DBGWCONNECTOR_FAILED;
  const static int MRSC_INVALID_GROUPADDRESS           = DBGWCONNECTOR_INVALID_PARAMETER;
  const static int MRSC_INVALID_DESTADDRESS            = DBGWCONNECTOR_INVALID_PARAMETER;
  const static int MRSC_NOTENOUGH_SENDBUF              = DBGWCONNECTOR_FAILED;
  const static int MRSC_DELIVER_FAIL                   = DBGWCONNECTOR_FAILED;

  DECLSPECIFIER unsigned int __stdcall GetLastError();
  DECLSPECIFIER int __stdcall GetLastInterfaceErrorCode();
  DECLSPECIFIER const char *__stdcall GetLastErrorMessage();
  DECLSPECIFIER const char *__stdcall GetFormattedErrorMessage();

  namespace Environment
  {

    typedef dbgw::DBGWConfiguration *Handle, *THandle;

    /**
     * this is for backward compatibility.
     */
    DECLSPECIFIER Handle __stdcall CreateHandle(size_t nThreadPoolSize);
    DECLSPECIFIER Handle __stdcall CreateHandle(const char *szConfFileName);
    DECLSPECIFIER void __stdcall DestroyHandle(Handle hEnv);


    DECLSPECIFIER void __stdcall SetDefaultTimeout(Handle hEnv,
        unsigned long ulMilliseconds);
    DECLSPECIFIER bool __stdcall GetDefaultTimeout(Handle hEnv,
        unsigned long *pTimeout);
    DECLSPECIFIER bool __stdcall LoadConnector(Handle hEnv);
    DECLSPECIFIER bool __stdcall LoadQueryMapper(Handle hEnv);
    /**
     * do not call this method directly.
     */
    DECLSPECIFIER bool __stdcall LoadConnector(Handle hEnv,
        const char *szConnectorFileName);
    DECLSPECIFIER bool __stdcall LoadQueryMapper(Handle hEnv,
        dbgw::DBGWQueryMapperVersion version, const char *szQueryMapFileName,
        bool bAppend = false);

  }

  namespace Connector
  {

    typedef Environment::Handle *Handle, *THandle;

    /**
     * this is for backward compatibility.
     */
    DECLSPECIFIER Handle __stdcall CreateHandle(Environment::Handle hEnv,
        const char *szMRSAddress);
    DECLSPECIFIER Handle __stdcall CreateHandle(Environment::Handle hEnv);
    DECLSPECIFIER void __stdcall DestroyHandle(Handle hEnv);

    DECLSPECIFIER void __stdcall SetDefaultTimeout(Handle hConnector,
        unsigned long ulMilliseconds);
    DECLSPECIFIER bool __stdcall GetDefaultTimeout(Handle hConnector,
        unsigned long *pulMilliseconds);
#ifdef ENABLE_UNUSED_FUNCTION
    /**
     * DBGW 3.0 cannot support this feature.
     */
    DECLSPECIFIER bool __stdcall SetLocalCharset(Handle hEnv,
        DBGW::CodePage codepage);
    DECLSPECIFIER DBGW::CodePage __stdcall GetLocalCharset(Handle hEnv);
#endif

  }

  namespace ParamSet
  {

    typedef dbgw::DBGWClientParameter *Handle, *THandle;

    DECLSPECIFIER Handle __stdcall CreateHandle();
    DECLSPECIFIER void __stdcall DestroyHandle(Handle hParam);

    DECLSPECIFIER bool __stdcall Clear(Handle hParam);
    DECLSPECIFIER size_t __stdcall Size(Handle hParam);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, int nParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        int nParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, char cParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        char cParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, int64 nParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        int64 nParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, float fParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        float fParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, double dParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        double dParamValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, const char *szParamValue, size_t nLen);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        const char *szParamValue, size_t nLen);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, size_t nSize, const void *pValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        size_t nSize, const void *pValue);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam,
        const char *szParamName, dbgw::DBGWValueType type, struct tm &value);
    DECLSPECIFIER bool __stdcall SetParameter(Handle hParam, int nIndex,
        dbgw::DBGWValueType type, struct tm &value);

  }

  namespace ParamList
  {
    typedef dbgw::DBGWClientParameterList *Handle, *Thandle;

    DECLSPECIFIER Handle __stdcall CreateHandle();
    DECLSPECIFIER void __stdcall DestroyHandle(Handle hParamList);

    DECLSPECIFIER size_t __stdcall GetSize(Handle hParamList);
    DECLSPECIFIER bool __stdcall Add(Handle hParamList, ParamSet::Handle hParam);
  }

  namespace ResultSetMeta
  {

    typedef dbgw::DBGWResultSetMetaDataSharedPtr Handle;

    DECLSPECIFIER size_t __stdcall GetColumnCount(Handle hMeta);
    DECLSPECIFIER bool __stdcall GetColumnName(Handle hMeta, size_t nIndex,
        const char **szName);
    DECLSPECIFIER bool __stdcall GetColumnType(Handle hMeta, size_t nIndex,
        dbgw::DBGWValueType *pType);

  }

  namespace ResultSet
  {

    typedef dbgw::DBGWClientResultSetSharedPtr *Handle, *THandle;

    DECLSPECIFIER Handle __stdcall CreateHandle();
    DECLSPECIFIER void  __stdcall DestroyHandle(Handle hResult);

    DECLSPECIFIER size_t __stdcall GetRowCount(Handle hResult);
    DECLSPECIFIER bool  __stdcall First(Handle hResult);
    DECLSPECIFIER bool  __stdcall Fetch(Handle hResult);
    DECLSPECIFIER size_t __stdcall GetAffectedCount(Handle hResult);
    DECLSPECIFIER bool __stdcall IsNeedFetch(Handle hResult);
    DECLSPECIFIER ResultSetMeta::Handle __stdcall GetMetaData(Handle hResult);
    DECLSPECIFIER bool __stdcall IsNull(Handle hResult, int nIndex,
        bool *pIsNull);
    DECLSPECIFIER bool __stdcall IsNull(Handle hResult, const char *szName,
        bool *pIsNull);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        int *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, int *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        int64 *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, int64 *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        float *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, float *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        double *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, double *pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        char *szBuffer, int BufferSize, size_t *pLen);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, char *szBuffer, int BufferSize, size_t *pLen);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult, int nIndex,
        size_t *pSize, char **pValue);
    DECLSPECIFIER bool __stdcall GetParameter(Handle hResult,
        const char *szName, size_t *pSize, char **pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        int *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        int *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        int64 *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        int64 *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        float *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        float *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        double *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        double *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        char *szBuffer, int BufferSize, size_t *pLen);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        char *szBuffer, int BufferSize, size_t *pLen);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        size_t *pSize, char **pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        size_t *pSize, char **pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, int nIndex,
        struct tm *pValue);
    DECLSPECIFIER bool __stdcall GetColumn(Handle hResult, const char *szName,
        struct tm *pValue);
    DECLSPECIFIER bool __stdcall GetType(Handle hResult, int nIndex,
        dbgw::DBGWValueType *pType);
    DECLSPECIFIER bool __stdcall GetType(Handle hResult, const char *szName,
        dbgw::DBGWValueType *pType);

  }

  namespace BatchResult
  {
    typedef dbgw::DBGWClientBatchResultSetSharedPtr *Handle, *THandle;

    DECLSPECIFIER Handle __stdcall CreateHandle();
    DECLSPECIFIER void  __stdcall DestroyHandle(Handle hBatchResult);

    DECLSPECIFIER bool __stdcall GetSize(Handle hBatchResult, int *pSize);
    DECLSPECIFIER bool __stdcall GetExecuteStatus(Handle hBatchResult,
        dbgw::DBGWBatchExecuteStatus *pStatus);
    DECLSPECIFIER bool __stdcall GetAffectedCount(Handle hBatchResult,
        int nIndex, int *pAffectedCount);
    DECLSPECIFIER bool __stdcall GetErrorCode(Handle hBatchResult,
        int nIndex, int *pErrorCode);
    DECLSPECIFIER bool __stdcall GetErrorMessage(Handle hBatchResult,
        int nIndex, const char **pErrorMessage);
    DECLSPECIFIER bool __stdcall GetStatementType(Handle hBatchResult,
        int nIndex, dbgw::DBGWStatementType *pStatementType);
  }

  namespace Executor
  {

    typedef dbgw::DBGWClient *Handle, *THandle;

    DECLSPECIFIER Handle __stdcall CreateHandle(DBGW3::Connector::Handle henv,
        const char *szNamespace = NULL);
    DECLSPECIFIER void __stdcall DestroyHandle(Handle hExecutor);

    DECLSPECIFIER bool __stdcall Execute(Handle hExecutor, const char *szMethod,
        DBGW3::ParamSet::Handle hParam, DBGW3::ResultSet::Handle &hResult);
    DECLSPECIFIER bool __stdcall Execute(Handle hExecutor, const char *szMethod,
        unsigned long ulMilliseconds, DBGW3::ParamSet::Handle hParam,
        DBGW3::ResultSet::Handle hResult);
    DECLSPECIFIER bool __stdcall ExecuteBatch(Handle hExecutor,
        const char *szMethod, DBGW3::ParamList::Handle hParamList,
        DBGW3::BatchResult::Handle &hBatchResult);
    DECLSPECIFIER bool __stdcall ExecuteBatch(Handle hExecutor,
        const char *szMethod, unsigned long ulMilliseconds,
        DBGW3::ParamList::Handle hParamList,
        DBGW3::BatchResult::Handle &hBatchResult);
    DECLSPECIFIER bool __stdcall BeginTransaction(Handle hExecutor);
    DECLSPECIFIER bool __stdcall BeginTransaction(Handle hExecutor,
        unsigned long ulWaitTimeMilSec);
    DECLSPECIFIER bool __stdcall CommitTransaction(Handle hExecutor);
    DECLSPECIFIER bool __stdcall CommitTransaction(Handle hExecutor,
        unsigned long ulWaitTimeMilSec);
    DECLSPECIFIER bool __stdcall RollbackTransaction(Handle hExecutor);
    DECLSPECIFIER bool __stdcall RollbackTransaction(Handle hExecutor,
        unsigned long ulWaitTimeMilSec);
#ifdef ENABLE_UNUSED_FUNCTION
    /**
     * DBGW 3.0 cannot support this feature.
     */
    DECLSPECIFIER bool __stdcall Execute(Handle hExecutor, const char *szMethod,
        unsigned long ulMilliseconds, DBGW::ParamSet::Handle hParam,
        DBGW::ResultSet::Handle hResult);
    DECLSPECIFIER bool __stdcall StartTransaction(Handle hExecutor,
        unsigned long ulMilliseconds);
    DECLSPECIFIER bool __stdcall BeginTransaction(Handle hExecutor,
        unsigned long ulMilliseconds);
    DECLSPECIFIER bool __stdcall CommitTransaction(Handle hExecutor,
        unsigned long ulMilliseconds);
    DECLSPECIFIER bool __stdcall RollbackTransaction(Handle hExecutor,
        unsigned long ulMilliseconds);
#endif

  }

  namespace Mock
  {

    typedef dbgw::_CCIMockManager *Handle;

    /**
     * do not call this method directly.
     */
    DECLSPECIFIER Handle __stdcall GetInstance();

    DECLSPECIFIER void __stdcall AddReturnErrorFault(Handle hMock,
        const char *szFaultFunction, dbgw::_CCI_FAULT_TYPE type,
        int nReturnCode = 0, int nErrorCode = 0, const char *szErrorMessage = "");
    DECLSPECIFIER void __stdcall AddSleepFault(Handle hMock,
        const char *szFaultFunction, dbgw::_CCI_FAULT_TYPE type,
        unsigned long ulSleepMilSec);
    DECLSPECIFIER void __stdcall ClearFaultAll(Handle hMock);

  }

}

namespace dbgw
{

  class DBGWPreviousException : public dbgw::DBGWException
  {
  public:
    DBGWPreviousException(const dbgw::DBGWExceptionContext &context) throw();
    virtual ~DBGWPreviousException() throw();

  private:
    DBGWPreviousException();
  };

  class DBGWPreviousExceptionFactory
  {
  public:
    static DBGWPreviousException create(const dbgw::DBGWException &e,
        int nPreviousErrorCdoe, const char *szPreviousErrorMessage);

  private:
    virtual ~DBGWPreviousExceptionFactory();
  };

}

#endif /* DBGWADAPTER_H_ */
