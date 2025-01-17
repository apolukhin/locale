//
// Copyright (c) 2009-2011 Artyom Beilis (Tonkikh)
//
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt

#define BOOST_LOCALE_SOURCE
#include <boost/locale/date_time.hpp>
#include <boost/locale/date_time_facet.hpp>
#include <boost/locale/formatting.hpp>
#include <boost/locale/hold_ptr.hpp>
#include "boost/locale/icu/all_generator.hpp"
#include "boost/locale/icu/cdata.hpp"
#include "boost/locale/icu/icu_util.hpp"
#include "boost/locale/icu/time_zone.hpp"
#include "boost/locale/icu/uconv.hpp"
#include <boost/thread.hpp>
#include <cmath>
#include <iostream>
#include <memory>
#include <unicode/calendar.h>
#include <unicode/gregocal.h>
#include <unicode/utypes.h>

namespace boost {
namespace locale {
namespace impl_icu {

    static void check_and_throw_dt(UErrorCode &e)
    {
        if(U_FAILURE(e)) {
            throw date_time_error(u_errorName(e));
        }
    }
    using period::marks::period_mark;

    static UCalendarDateFields to_icu(period::marks::period_mark f)
    {
        using namespace period::marks;

        switch(f) {
        case era: return UCAL_ERA;
        case year: return UCAL_YEAR;
        case extended_year: return UCAL_EXTENDED_YEAR;
        case month: return UCAL_MONTH;
        case day: return UCAL_DATE;
        case day_of_year: return UCAL_DAY_OF_YEAR;
        case day_of_week: return UCAL_DAY_OF_WEEK;
        case day_of_week_in_month:  return UCAL_DAY_OF_WEEK_IN_MONTH;
        case day_of_week_local: return UCAL_DOW_LOCAL;
        case hour: return UCAL_HOUR_OF_DAY;
        case hour_12: return UCAL_HOUR;
        case am_pm: return UCAL_AM_PM;
        case minute: return UCAL_MINUTE;
        case second: return UCAL_SECOND;
        case week_of_year: return UCAL_WEEK_OF_YEAR;
        case week_of_month: return UCAL_WEEK_OF_MONTH;
        default:
            throw std::invalid_argument("Invalid date_time period type");
        }
    }


    class calendar_impl : public abstract_calendar {
    public:

        calendar_impl(cdata const &dat)
        {
            UErrorCode err=U_ZERO_ERROR;
            calendar_.reset(icu::Calendar::createInstance(dat.locale,err));
            check_and_throw_dt(err);
            #if BOOST_LOCALE_ICU_VERSION < 402
            // workaround old/invalid data, it should be 4 in general
            calendar_->setMinimalDaysInFirstWeek(4);
            #endif
            encoding_ = dat.encoding;
        }
        calendar_impl(calendar_impl const &other)
        {
            calendar_.reset(other.calendar_->clone());
            encoding_ = other.encoding_;
        }

        calendar_impl *clone() const override
        {
            return new calendar_impl(*this);
        }

        void set_value(period::marks::period_mark p,int value) override
        {
            calendar_->set(to_icu(p),int32_t(value));
        }

        int get_value(period::marks::period_mark p,value_type type) const override
        {
            UErrorCode err=U_ZERO_ERROR;
            int v=0;
            if(p==period::marks::first_day_of_week) {
                guard l(lock_);
                v=calendar_->getFirstDayOfWeek(err);
            }
            else {
                UCalendarDateFields uper=to_icu(p);
                guard l(lock_);
                switch(type) {
                case absolute_minimum:
                    v=calendar_->getMinimum(uper);
                    break;
                case actual_minimum:
                    v=calendar_->getActualMinimum(uper,err);
                    break;
                case greatest_minimum:
                    v=calendar_->getGreatestMinimum(uper);
                    break;
                case current:
                    v=calendar_->get(uper,err);
                    break;
                case least_maximum:
                    v=calendar_->getLeastMaximum(uper);
                    break;
                case actual_maximum:
                    v=calendar_->getActualMaximum(uper,err);
                    break;
                case absolute_maximum:
                    v=calendar_->getMaximum(uper);
                    break;
                }
            }
            check_and_throw_dt(err);
            return v;
        }

        void set_time(posix_time const &p) override
        {
            double utime = p.seconds * 1000.0 + p.nanoseconds / 1000000.0;
            UErrorCode code=U_ZERO_ERROR;
            calendar_->setTime(utime,code);
            check_and_throw_dt(code);
        }
        void normalize() override
        {
            // Can't call complete() explicitly (protected)
            // calling get which calls complete
            UErrorCode code=U_ZERO_ERROR;
            calendar_->get(UCAL_YEAR,code);
            check_and_throw_dt(code);
        }
        posix_time get_time() const override
        {

            UErrorCode code=U_ZERO_ERROR;
            double rtime = 0;
            {
                guard l(lock_);
                rtime = calendar_->getTime(code);
            }
            check_and_throw_dt(code);
            rtime/=1000.0;
            double secs = floor(rtime);
            posix_time res;
            res.seconds = static_cast<int64_t>(secs);
            res.nanoseconds = static_cast<uint32_t>((rtime - secs) / 1e9);
            if(res.nanoseconds > 999999999)
                res.nanoseconds = 999999999;
            return res;
        }
        void set_option(calendar_option_type opt,int /*v*/) override
        {
            switch(opt) {
            case is_gregorian:
                throw date_time_error("is_gregorian is not settable options for calendar");
            case is_dst:
                throw date_time_error("is_dst is not settable options for calendar");
            default:
                ;
            }
        }
        int get_option(calendar_option_type opt) const override
        {
            switch(opt) {
            case is_gregorian:
                return dynamic_cast<icu::GregorianCalendar const *>(calendar_.get())!=0;
            case is_dst:
                {
                    guard l(lock_);
                    UErrorCode err = U_ZERO_ERROR;
                    bool res = ( calendar_->inDaylightTime(err) != 0 );
                    check_and_throw_dt(err);
                    return res;
                }
            default:
                return 0;
            }
        }
        void adjust_value(period::marks::period_mark p,update_type u,int difference) override
        {
            UErrorCode err=U_ZERO_ERROR;
            switch(u) {
            case move:
                calendar_->add(to_icu(p),difference,err);
                break;
            case roll:
                calendar_->roll(to_icu(p),difference,err);
                break;
            }
            check_and_throw_dt(err);
        }
        int difference(abstract_calendar const *other_ptr, period::marks::period_mark m) const override
        {
            UErrorCode err=U_ZERO_ERROR;
            double other_time = 0;
            //
            // fieldDifference has side effect of moving calendar (WTF?)
            // So we clone it for performing this operation
            //
            hold_ptr<icu::Calendar> self(calendar_->clone());

            calendar_impl const *other_cal=dynamic_cast<calendar_impl const *>(other_ptr);
            if(other_cal){
                guard l(other_cal->lock_);
                other_time = other_cal->calendar_->getTime(err);
                check_and_throw_dt(err);
            }
            else {
                posix_time o_time = other_ptr->get_time();
                other_time = o_time.seconds * 1000.0 + o_time.nanoseconds / 1000000.0;
            }

            int diff = self->fieldDifference(other_time,to_icu(m),err);

            check_and_throw_dt(err);
            return diff;
        }
        void set_timezone(std::string const &tz) override
        {
            calendar_->adoptTimeZone(get_time_zone(tz));
        }
        std::string get_timezone() const override
        {
            icu::UnicodeString tz;
            calendar_->getTimeZone().getID(tz);
            icu_std_converter<char> cvt(encoding_);
            return cvt.std(tz);
        }
        bool same(abstract_calendar const *other) const override
        {
            calendar_impl const *oc=dynamic_cast<calendar_impl const *>(other);
            if(!oc)
                return false;
            return calendar_->isEquivalentTo(*oc->calendar_)!=0;
        }

    private:
        typedef boost::unique_lock<boost::mutex> guard;
        mutable boost::mutex lock_;
        std::string encoding_;
        hold_ptr<icu::Calendar> calendar_;
    };

    class icu_calendar_facet : public calendar_facet  {
    public:
        icu_calendar_facet(cdata const &d,size_t refs = 0): calendar_facet(refs), data_(d) {}
        abstract_calendar *create_calendar() const override
        {
            return new calendar_impl(data_);
        }
    private:
        cdata data_;
    };

    std::locale create_calendar(std::locale const &in,cdata const &d)
    {
        return std::locale(in,new icu_calendar_facet(d));
    }

} // impl_icu
} // locale
} // boost




