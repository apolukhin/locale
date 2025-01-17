//
// Copyright (c) 2009-2011 Artyom Beilis (Tonkikh)
//
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt

#ifdef BOOST_LOCALE_NO_POSIX_BACKEND
#include <iostream>
int main()
{
        std::cout << "POSIX Backend is not build... Skipping\n";
}
#else
#include <boost/locale/formatting.hpp>
#include <boost/locale/encoding.hpp>
#include <boost/locale/generator.hpp>
#include <boost/locale/info.hpp>
#include <boost/locale/localization_backend.hpp>
#include <iomanip>
#include <ctime>
#include <iostream>
#include <monetary.h>
#include <langinfo.h>

#include "boostLocale/test/unit_test.hpp"
#include "boostLocale/test/tools.hpp"

//#define DEBUG_FMT

bool equal(std::string const &s1,std::string const &s2,locale_t /*lc*/)
{
    return s1 == s2;
}

bool equal(std::wstring const &s1,std::string const &s2,locale_t lc)
{
    return s1 == boost::locale::conv::to_utf<wchar_t>(s2,nl_langinfo_l(CODESET,lc));
}

template<typename CharType>
std::basic_string<CharType> conv_to_char(char const *p)
{
    std::basic_string<CharType> r;
    while(*p)
        r+=CharType(*p++);
    return r;
}


template<typename CharType,typename RefCharType>
void test_by_char(std::locale const &l,locale_t lreal)
{
    typedef std::basic_stringstream<CharType> ss_type;

    using namespace boost::locale;

    {
        std::cout << "- Testing as::posix" << std::endl;
        ss_type ss;
        ss.imbue(l);

        ss << 1045.45;
        TEST(ss);
        double n;
        ss >> n;
        TEST(ss);
        TEST(n == 1045.45);
        TEST(ss.str()==to_correct_string<CharType>("1045.45",l));
        #ifdef DEBUG_FMT
        std::cout << "[" << boost::locale::conv::from_utf(ss.str(),"UTF-8") << "]=\n" ;
        #endif
    }

    {
        std::cout << "- Testing as::number" << std::endl;
        ss_type ss;
        ss.imbue(l);

        ss << as::number;
        ss << 1045.45;
        TEST(ss);
        double n;
        ss >> n;
        TEST(ss);
        TEST(n == 1045.45);

        if(std::use_facet<boost::locale::info>(l).country()=="US")
            TEST(equal(ss.str(),"1,045.45",lreal));
    }

    {
        std::cout << "- Testing as::currency national " << std::endl;

        char buf[64];
        TEST(strfmon_l(buf,sizeof(buf),lreal,"%n",1043.34) > 0);

        ss_type ss;
        ss.imbue(l);

        ss << as::currency;
        ss << 1043.34;
        TEST(ss);

        TEST(equal(ss.str(),buf,lreal));
        #ifdef DEBUG_FMT
        std::cout << "[" << boost::locale::conv::from_utf(ss.str(),"UTF-8") << "]=\n" ;
        std::cout << "[" << boost::locale::conv::from_utf(buf,"UTF-8") << "]\n" ;
        #endif

    }

    {
        std::cout << "- Testing as::currency iso" << std::endl;
        char buf[64];
        TEST(strfmon_l(buf,sizeof(buf),lreal,"%i",1043.34) > 0);
        ss_type ss;
        ss.imbue(l);

        ss << as::currency << as::currency_iso;
        ss << 1043.34;
        TEST(ss);

        TEST(equal(ss.str(),buf,lreal));
        #ifdef DEBUG_FMT
        std::cout << "[" << boost::locale::conv::from_utf(ss.str(),"UTF-8") << "]=\n" ;
        std::cout << "[" << boost::locale::conv::from_utf(buf,"UTF-8") << "]\n" ;
        #endif
    }


    {
        std::cout << "- Testing as::date/time" << std::endl;
        ss_type ss;
        ss.imbue(l);

        time_t a_date = 3600*24*(31+4); // Feb 5th
        time_t a_time = 3600*15+60*33; // 15:33:05
        time_t a_timesec = 13;
        time_t a_datetime = a_date + a_time + a_timesec;

        ss << as::time_zone("GMT");

        ss << as::date << a_datetime << CharType('\n');
        ss << as::time << a_datetime << CharType('\n');
        ss << as::datetime << a_datetime << CharType('\n');
        ss << as::time_zone("GMT+01:00");
        ss << as::ftime(conv_to_char<CharType>("%H")) << a_datetime << CharType('\n');
        ss << as::time_zone("GMT+00:15");
        ss << as::ftime(conv_to_char<CharType>("%M")) << a_datetime << CharType('\n');

        std::tm tm=*gmtime(&a_datetime);
        char buf[64];
        TEST(strftime_l(buf,sizeof(buf),"%x\n%X\n%c\n16\n48\n",&tm,lreal) > 0);

        TEST(equal(ss.str(),buf,lreal));
        #ifdef DEBUG_FMT
        std::cout << "[" << boost::locale::conv::from_utf(ss.str(),"UTF-8") << "]=\n" ;
        std::cout << "[" << boost::locale::conv::from_utf(buf,"UTF-8") << "]\n" ;
        #endif
    }

}

void test_main(int /*argc*/, char** /*argv*/)
{
    boost::locale::localization_backend_manager mgr = boost::locale::localization_backend_manager::global();
    mgr.select("posix");
    boost::locale::localization_backend_manager::global(mgr);
    boost::locale::generator gen;
    for(const std::string locale_name : {"en_US.UTF-8", "en_US.ISO8859-1", "he_IL.UTF-8", "he_IL.ISO8859-8"})
    {
        std::cout << locale_name << " locale" << std::endl;
        if(!have_locale(locale_name))
        {
            std::cout << locale_name << " not supported" << std::endl;
        } else
        {
            std::locale generated_locale=gen(locale_name);
            locale_holder real_locale(newlocale(LC_ALL_MASK,locale_name.c_str(),0));
            TEST_REQUIRE(real_locale);

            std::cout << "UTF-8" << std::endl;
            test_by_char<char,char>(generated_locale,real_locale);

            std::cout << "Wide UTF-" << sizeof(wchar_t) * 8 << std::endl;
            test_by_char<wchar_t,char>(generated_locale,real_locale);
        }
    }
    {
        std::cout << "Testing UTF-8 punct issues" << std::endl;
        const std::string locale_name = "ru_RU.UTF-8";
        if(!have_locale(locale_name))
        {
            std::cout << "- No Russian locale" << std::endl;
        } else {
            std::ostringstream ss;
            ss.imbue(gen(locale_name));
            ss << std::setprecision(10) << boost::locale::as::number << 12345.45;
            const std::string v=ss.str();
            TEST(v == "12345,45" || v == "12 345,45" || v=="12.345,45");
        }
    }
}

#endif // posix

// boostinspect:noascii
