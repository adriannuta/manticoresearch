//
// Copyright (c) 2019, Manticore Software LTD (http://manticoresearch.com)
// All rights reserved
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License. You should have
// received a copy of the GPL license along with this program; if you
// did not, you can find it at http://www.gnu.org/
//
/// @file threadutils.h
/// Different thread-related things (moved here from searchd.cpp)

#ifndef MANTICORE_THREADUTILS_H
#define MANTICORE_THREADUTILS_H

#include "sphinxstd.h"
#include "sphinxint.h"

struct CSphQuery;

enum class ThdState_e
{
	HANDSHAKE,
	NET_READ,
	NET_WRITE,
	QUERY,
	NET_IDLE,
};

enum class Proto_e
{
	SPHINX,
	MYSQL41,
	HTTP,
	REPLICATION,
	HTTPS
};

const char* ThdStateName( ThdState_e eState);
const char* ProtoName ( Proto_e eProto );

int GetOsThreadId ();

namespace Threads {

struct ServiceThread_t
{
	SphThread_t m_tThread;
	bool m_bCreated = false;

	~ServiceThread_t ();
	bool Create ( void (* fnThread) ( void* ), void* pArg, const char* sName = nullptr );
	void Join ();
};

// common thread properties
struct ThdInfo_t
{
	Proto_e	   m_eProto		 = Proto_e::MYSQL41;
	int		   m_iClientSock = 0;
	CSphString m_sClientName;
	bool	   m_bVip = false;

	ThdState_e	m_eThdState = ThdState_e::HANDSHAKE;
	const char* m_sCommand	= nullptr;
	int			m_iConnID	= -1; ///< current conn-id for this thread

	// stuff for SHOW THREADS
	int		m_iTid		= 0; ///< OS thread id, or 0 if unknown
	int64_t m_tmConnect = 0; ///< when did the client connect?
	int64_t m_tmStart	= 0; ///< when did the current request start?
	bool	m_bSystem	= false;
	int		m_iCookie	= 0; ///< may be used in case of pool to distinguish threads
};

// trivial info for public use (no locks, everything owned)
struct ThdPublicInfo_t : ThdInfo_t, public ISphNoncopyable
{
	CSphString m_sThName;
	CSphQuery* m_pQuery = nullptr;
	CSphString m_sRequestDescription;

	ThdPublicInfo_t() = default;
	~ThdPublicInfo_t();

	void Swap( ThdPublicInfo_t& rhs );
	ThdPublicInfo_t( ThdPublicInfo_t&& rhs) noexcept;
	ThdPublicInfo_t& operator= ( ThdPublicInfo_t&& rhs) noexcept;
};



struct ThdDesc_t: public ThdInfo_t, private ListNode_t
{
	SphThread_t m_tThd { 0 };
	CSphMutex		 m_tQueryLock;
	const CSphQuery* m_pQuery GUARDED_BY ( m_tQueryLock ) = nullptr;
	StringBuilder_c m_sBuf GUARDED_BY ( m_tQueryLock ); ///< current request description

	void SetThreadInfo ( const char* sTemplate, ... ) REQUIRES ( !m_tQueryLock );
	void SetSearchQuery ( const CSphQuery* pQuery ) REQUIRES ( !m_tQueryLock );

	void AddToGlobal ();
	void RemoveFromGlobal ();
	ThdPublicInfo_t GetPublicInfo();
};

struct ThreadSystem_t
{
	ThdDesc_t m_tDesc;
	explicit ThreadSystem_t ( const char* sName );
	~ThreadSystem_t ();
};

struct ThreadLocal_t
{
	ThdDesc_t m_tDesc;
	explicit ThreadLocal_t ( const ThdDesc_t& tDesc );
	~ThreadLocal_t ();
};

int ThreadsNum ();
void ThdState ( ThdState_e eState, ThdDesc_t& tThd );

// WARNING! Should be used only in crash dumping, where any copying is UB!
List_t& GetUnsafeUnlockedUnprotectedGlobalThreadList();

// Trivial vec of global thread descriptors
CSphSwapVector<ThdPublicInfo_t> GetGlobalThreadInfos ();

} // namespace Threads



extern ThreadRole MainThread;

/// This class is basically a pointer to query string and some more additional info.
/// Each thread which executes query must have exactly one instance of this class on
/// its stack and m_tLastQueryTLS will contain a pointer to that instance.
/// Main thread has explicitly created SphCrashLogger_c on its stack, other query
/// threads should be created with SphCrashLogger_c::ThreadCreate()
class SphCrashLogger_c
{
public:
	SphCrashLogger_c()
	{}

	~SphCrashLogger_c();

	static void Init() REQUIRES ( MainThread );

	static void Done() REQUIRES ( MainThread )
	{};

#if !USE_WINDOWS
	static void HandleCrash( int );
#else
	static LONG WINAPI HandleCrash ( EXCEPTION_POINTERS * pExc );
#endif
	static void SetLastQuery( const CrashQuery_t& tQuery );
	static void SetupTimePID();
	static CrashQuery_t GetQuery();
	static void SetTopQueryTLS( CrashQuery_t* pQuery );

	// create thread with crash logging
	static bool ThreadCreate( SphThread_t* pThread, void ( * pCall )( void* ), void* pArg, bool bDetached = false,
		const char* sName = nullptr );

private:
	struct CallArgPair_t
	{
		CallArgPair_t( void ( * pCall )( void* ), void* pArg )
			: m_pCall( pCall ), m_pArg( pArg )
		{}

		void ( * m_pCall )( void* );
		void* m_pArg;
	};

	// sets up a TLS for a given thread
	static void ThreadWrapper( void* pArg );
	static TLS_T<CrashQuery_t> m_pTlsCrashQuery;    // pointer to on-stack instance of this class
};


#endif //MANTICORE_THREADUTILS_H
