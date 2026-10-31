// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "FaceWatch.h"

#include "Debug.h"

#include <set>
#include <map>

#include <TlHelp32.h>

namespace FaceWatch
{
	namespace
	{
		struct Slot
		{
			std::atomic<std::uintptr_t> want{ 0 };
			std::atomic<RE::FormID> who{ 0 };
			std::atomic<const char*> what{ "" };
		};
		std::array<Slot, 4> g_slots;
		std::array<std::uintptr_t, 4> g_armed{};  // what the threads were last given (the watch thread only)

		// One write, as the exception handler saw it. The handler takes no lock and allocates nothing: a fixed ring, filled with an atomic counter.
		struct Hit
		{
			std::uintptr_t rip;
			std::uint32_t tid;
			float value;
			int slot;
		};
		constexpr std::size_t kRing = 4096;
		std::array<Hit, kRing> g_ring{};
		std::atomic<std::uint64_t> g_written{ 0 };
		std::atomic<std::uint64_t> g_ready{ 0 };
		std::uint64_t g_read = 0;
		std::atomic<std::uint64_t> g_dropped{ 0 };

		LONG CALLBACK Handler(EXCEPTION_POINTERS* a_info)
		{
			if (a_info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
			auto* ctx = a_info->ContextRecord;
			const auto status = ctx->Dr6 & 0xF;
			if (!status) return EXCEPTION_CONTINUE_SEARCH;  // a single step of someone else's
			for (int i = 0; i < 4; ++i) {
				if (!(status & (1ull << i))) continue;
				const auto addr = i == 0 ? ctx->Dr0 : i == 1 ? ctx->Dr1 : i == 2 ? ctx->Dr2 : ctx->Dr3;
				const auto n = g_written.fetch_add(1, std::memory_order_relaxed);
				if (n - g_read >= kRing) {
					g_dropped.fetch_add(1, std::memory_order_relaxed);
					continue;
				}
				auto& h = g_ring[n % kRing];
				h.rip = reinterpret_cast<std::uintptr_t>(a_info->ExceptionRecord->ExceptionAddress);
				h.tid = ::GetCurrentThreadId();
				h.value = addr ? *reinterpret_cast<const float*>(addr) : 0.0f;
				h.slot = i;
				g_ready.fetch_add(1, std::memory_order_release);
			}
			ctx->Dr6 = 0;
			return EXCEPTION_CONTINUE_EXECUTION;
		}

		// Gives every thread of the game the four addresses (write, 4 bytes). One thread at a time is suspended, given them and resumed, and nothing is
		// allocated while one is suspended, so a thread stopped holding the heap's lock cannot hang this one.
		int Arm(const std::array<std::uintptr_t, 4>& a)
		{
			std::vector<DWORD> ids;
			ids.reserve(512);
			const auto pid = ::GetCurrentProcessId();
			const auto self = ::GetCurrentThreadId();
			HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
			if (snap == INVALID_HANDLE_VALUE) return 0;
			THREADENTRY32 te{ sizeof(te) };
			for (BOOL ok = ::Thread32First(snap, &te); ok; ok = ::Thread32Next(snap, &te)) {
				if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) ids.push_back(te.th32ThreadID);
			}
			::CloseHandle(snap);
			DWORD64 dr7 = 0;
			for (int i = 0; i < 4; ++i) {
				if (!a[static_cast<std::size_t>(i)]) continue;
				dr7 |= 1ull << (2 * i);          // local enable
				dr7 |= 1ull << (16 + 4 * i);     // break on write
				dr7 |= 3ull << (18 + 4 * i);     // 4 bytes
			}
			int done = 0;
			for (const auto id : ids) {
				HANDLE t = ::OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, id);
				if (!t) continue;
				if (::SuspendThread(t) != static_cast<DWORD>(-1)) {
					CONTEXT c{};
					c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
					if (::GetThreadContext(t, &c)) {
						c.Dr0 = a[0];
						c.Dr1 = a[1];
						c.Dr2 = a[2];
						c.Dr3 = a[3];
						c.Dr7 = dr7;
						if (::SetThreadContext(t, &c)) ++done;
					}
					::ResumeThread(t);
				}
				::CloseHandle(t);
			}
			return done;
		}

		std::string Where(std::uintptr_t a_rip)
		{
			HMODULE mod = nullptr;
			if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(a_rip), &mod))
				return std::format("{:X} (no module)", a_rip);
			wchar_t path[MAX_PATH]{};
			::GetModuleFileNameW(mod, path, MAX_PATH);
			return std::format("{}+{:X}", std::filesystem::path(path).filename().string(), a_rip - reinterpret_cast<std::uintptr_t>(mod));
		}

		struct Agg
		{
			int count = 0;
			int zeros = 0;
			float lo = 2.0f, hi = -1.0f;
			std::uint32_t tid = 0;
		};
		std::map<std::pair<int, std::uintptr_t>, Agg> g_total;  // since the last summary
		std::set<std::pair<int, std::uintptr_t>> g_named;      // sites already written out once

		// Once a second: a line for each piece of code that wrote a watched value for the first time, or wrote a zero into it; every 10 s, the totals by code.
		// The game's face update and OSIS write these values every frame, so their lines come once and then only in the summary.
		void Drain(bool a_summary)
		{
			const auto end = std::min(g_ready.load(std::memory_order_acquire), g_written.load(std::memory_order_relaxed));
			std::map<std::pair<int, std::uintptr_t>, Agg> agg;
			for (; g_read < end; ++g_read) {
				const auto& h = g_ring[g_read % kRing];
				for (auto* m : { &agg, &g_total }) {
					auto& x = (*m)[{ h.slot, h.rip }];
					++x.count;
					if (h.value <= 0.005f) ++x.zeros;
					x.lo = std::min(x.lo, h.value);
					x.hi = std::max(x.hi, h.value);
					x.tid = h.tid;
				}
			}
			const auto line = [](const std::pair<int, std::uintptr_t>& key, const Agg& x, const char* lead) {
				const auto& s = g_slots[static_cast<std::size_t>(key.first)];
				Debug::Log(Debug::Cat::Face, "{}: write watch{}, {}: {} write(s) by {} (thread {}), values {:.2f}..{:.2f}, {} of them zero", Debug::Label(s.who.load()), lead, s.what.load(),
					x.count, Where(key.second), x.tid, x.lo, x.hi, x.zeros);
			};
			for (const auto& [key, x] : agg) {
				const bool fresh = g_named.insert(key).second;
				if (fresh || x.zeros > 0) line(key, x, fresh ? " (new writer)" : "");
			}
			if (a_summary) {
				for (const auto& [key, x] : g_total) line(key, x, " (10 s total)");
				g_total.clear();
			}
			if (const auto d = g_dropped.exchange(0)) Debug::Log(Debug::Cat::Face, "write watch: {} write(s) not recorded (the ring was full)", d);
		}

		void Loop()
		{
			bool armed = false;
			unsigned ticks = 0;
			auto lastArm = std::chrono::steady_clock::now();
			for (;;) {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				const bool on = Debug::On();
				std::array<std::uintptr_t, 4> want{};
				for (std::size_t i = 0; i < 4; ++i) want[i] = on ? g_slots[i].want.load() : 0;
				const bool any = std::any_of(want.begin(), want.end(), [](auto v) { return v != 0; });
				const auto now = std::chrono::steady_clock::now();
				// Threads started since the last pass have none: armed again every 10 s while watching.
				if (want != g_armed || (any && now - lastArm > std::chrono::seconds(10))) {
					const int n = Arm(want);
					if (want != g_armed) {
						if (any) Debug::Log(Debug::Cat::Face, "write watch: {} address(es) watched in {} thread(s)", std::count_if(want.begin(), want.end(), [](auto v) { return v != 0; }), n);
						else if (armed) Debug::Log(Debug::Cat::Face, "write watch: off");
					}
					g_armed = want;
					armed = any;
					lastArm = now;
				}
				if (on && ++ticks % 4 == 0) Drain(ticks % 40 == 0);
			}
		}
	}

	void Want(int a_slot, const float* a_address, RE::FormID a_who, const char* a_what)
	{
		if (a_slot < 0 || a_slot > 3) return;
		auto& s = g_slots[static_cast<std::size_t>(a_slot)];
		s.who = a_who;
		s.what = a_what;
		s.want = reinterpret_cast<std::uintptr_t>(a_address);
	}

	void Start()
	{
		static std::once_flag once;
		std::call_once(once, [] {
			::AddVectoredExceptionHandler(1, Handler);
			std::thread(Loop).detach();
		});
	}
}
