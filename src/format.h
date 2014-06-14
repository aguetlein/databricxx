// Copyright (C) 2014 Oliver Schulz <oschulz@mpp.mpg.de>

// This is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or
// (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.


#ifndef DBRX_FORMAT_H
#define DBRX_FORMAT_H

#include <string>
#include <sstream>
#include <stdexcept>
#include <cstdint>


namespace dbrx {


class FormatString {
protected:
	class ConstCharRange: public std::pair<const char*, const char*> {
	public:
		const char* begin() const { return this->first; }
		const char* end() const { return this->second; }

		bool empty() { return begin() == end(); }
		size_t size() { return end() - begin(); }

		const char operator[](size_t idx) const { return begin()[idx]; }

		using std::pair<const char*, const char*>::pair;
	};

	static bool isFormatFlag(char c);
	static ConstCharRange findFmtElem(ConstCharRange str);

	std::string m_formatStr;

protected:
    struct UniPrintGeneral {};
    struct UniPrintSpecial : UniPrintGeneral {};

	static const std::ostream& universalPrint(std::ostream& os, const std::string &s, UniPrintSpecial) { return os << s; }

	template<typename T> static auto universalPrint(std::ostream& os, const T& x, UniPrintSpecial)
		-> decltype(os << x) { return os << x; }

	template<typename T> static auto universalPrint(std::ostream& os, const T& x, UniPrintGeneral)
		-> decltype(os << to_string(x)) { using namespace std; return os << to_string(x); }


	void printFormatted(std::ostream& os, ConstCharRange fmt, ...);

	void printFormattedValue(std::ostream& os, ConstCharRange fmt, char x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, wchar_t x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, int8_t x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, uint8_t x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, int16_t x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, uint16_t x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, int32_t x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, uint32_t x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, int64_t x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, uint64_t x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, float x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, double x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, long double x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, char* x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, wchar_t* x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, int8_t* x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, int16_t* x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, int32_t* x) { printFormatted(os, fmt, x); }
	void printFormattedValue(std::ostream& os, ConstCharRange fmt, int64_t* x) { printFormatted(os, fmt, x); }


	template <typename T> void printFormattedValue(std::ostream& os, ConstCharRange fmt, T x)
		{ throw std::invalid_argument("Argument type not supported for string format specifier"); }

	template <typename T, typename ...Args>
	void applyValues(std::ostream& os, ConstCharRange fmt, const T& x, Args&&... args) {
		using namespace std;
		ConstCharRange fmtElem = findFmtElem(fmt);
		if (fmtElem.empty()) throw std::invalid_argument("Less format elements in format string than arguments.");
		for (char c: ConstCharRange(fmt.begin(), fmtElem.begin())) os << c;
		if ((fmtElem.size() == 2) && (fmtElem[0] == '%') && (fmtElem[1] == 's')) universalPrint(os, x, UniPrintSpecial());
		else printFormattedValue(os, fmtElem, x);
		applyValues(os, {fmtElem.end(), fmt.end()}, std::forward<Args>(args)...);
	}

	void applyValues(std::ostream& os, ConstCharRange fmt) {
		if (!findFmtElem(fmt).empty()) throw std::invalid_argument("More format elements in format string than arguments.");
		else for (char c: fmt) os << c;
	}

public:
	const std::string& str() const { return m_formatStr; }

	template <typename ...Args> std::string operator()(Args&&... args) {
		std::stringstream tmp;
		print(tmp, std::forward<Args>(args)...);
		return tmp.str();
	}

	template <typename ...Args> std::ostream& print(std::ostream& os, Args&&... args) {
		applyValues(os, ConstCharRange(m_formatStr.c_str(), m_formatStr.c_str() + m_formatStr.size()),
			std::forward<Args>(args)...);
		return os;
	}

	operator const std::string& () const { return str(); }

	FormatString(std::string fmt): m_formatStr(std::move(fmt)) {}

	friend const std::string& to_string(const FormatString &fmt) { return fmt.str(); }
	friend std::ostream& operator<<(std::ostream &os, const FormatString &fmt) { return os << fmt.str(); }
};


inline FormatString operator"" _format (const char* str, std::size_t len) { return FormatString({str, len}); }
inline FormatString operator"" _format (const char* str) { return FormatString(str); }


} // namespace dbrx

#endif // DBRX_FORMAT_H