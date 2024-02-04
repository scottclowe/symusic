//
// Created by lyk on 23-12-16.
// This file is now much too confusing to read.
// Refactor it later.
//
#include <cmath>
#include "symusic/conversion.h"
#include "symusic/ops.h"

namespace symusic {

namespace details {

template<TType To, TType From, typename Converter>
Score<To> convertInner(const Score<From> & score, const Converter & converter, const typename To::unit min_dur) {
    Score<To> new_s(score.ticks_per_quarter);
    new_s.time_signatures = std::move(converter.time_vec(score.time_signatures));
    new_s.key_signatures = std::move(converter.time_vec(score.key_signatures));
    new_s.tempos = std::move(converter.time_vec(score.tempos));
    new_s.lyrics = std::move(converter.time_vec(score.lyrics));
    new_s.markers = std::move(converter.time_vec(score.markers));
    new_s.tracks.reserve(score.tracks.size());
    for(const Track<From> &track: score.tracks) {
        Track<To> new_t(track.name, track.program, track.is_drum);
        new_t.notes = std::move(converter.duration_vec(track.notes, min_dur));
        new_t.pedals = std::move(converter.duration_vec(track.pedals, min_dur));
        new_t.pitch_bends = std::move(converter.time_vec(track.pitch_bends));
        new_t.controls = std::move(converter.time_vec(track.controls));
        new_s.tracks.emplace_back(std::move(new_t));
    }   return new_s;
}

template<typename Converter, TType To, TType From>
struct SimpleConverter {
    template<template<typename> typename T>
    [[nodiscard]] vec<T<To>> time_vec(const vec<T<From>> & data) const {
        const auto self = static_cast<const Converter*>(this);
        vec<T<To>> ans;
        ans.reserve(data.size());
        for(const auto & d : data) {
            ans.emplace_back(self->time(d.time), d);
        }   return ans;
    }

    template<template<class> class T>
    [[nodiscard]] vec<T<To>> duration_vec(const vec<T<From>> & data, typename To::unit min_dur) const {
        const auto self = static_cast<const Converter*>(this);
        min_dur = std::max(min_dur, static_cast<typename To::unit>(0));
        vec<T<To>> ans;
        ans.reserve(data.size());
        for(const auto & d : data) {
            ans.emplace_back(
                self -> time(d.time),
                std::max(min_dur, self->time(d.duration)),
                d
            );
        }   return ans;
    }
};

struct Tick2Tick: SimpleConverter<Tick2Tick, Tick, Tick> {
    typedef Tick From;
    typedef Tick To;

    explicit Tick2Tick(const Score<From> & score) {}
    [[nodiscard]] To::unit time(const From::unit t) const { return t; }
};

struct Quarter2Quarter: SimpleConverter<Quarter2Quarter, Quarter, Quarter> {
    typedef Quarter From;
    typedef Quarter To;

    explicit Quarter2Quarter(const Score<From> & score) {}
    [[nodiscard]] To::unit time(const From::unit t) const { return t; }
};

struct Second2Second: SimpleConverter<Second2Second, Second, Second> {
    typedef Second From;
    typedef Second To;

    explicit Second2Second(const Score<From> & score) {}
    [[nodiscard]] To::unit time(const From::unit t) const { return t; }
};

struct Tick2Quarter: SimpleConverter<Tick2Quarter, Quarter, Tick> {
    typedef Tick From;
    typedef Quarter To;
    f32 tpq;

    explicit Tick2Quarter(const Score<From> & score): tpq(static_cast<f32>(score.ticks_per_quarter)) {}
    [[nodiscard]] To::unit time(const From::unit t) const { return static_cast<To::unit>(t) / tpq; }
};

struct Quarter2Tick: SimpleConverter<Quarter2Tick, Tick, Quarter> {
    typedef Quarter From;
    typedef Tick To;
    f32 tpq;

    explicit Quarter2Tick(const Score<From> & score): tpq(static_cast<f32>(score.ticks_per_quarter)) {}
    [[nodiscard]] To::unit time(const From::unit t) const { return static_cast<To::unit>(std::round(t * tpq)); }
};

template<typename Converter, TType To, TType From>
struct SecondConverter {
    f64 tpq;
    vec<Tempo<From>> tempos{};

    explicit SecondConverter(const Score<From> & score): tpq(static_cast<f64>(score.ticks_per_quarter)){
        if(score.tempos.empty()) {
            // 120 qpm
            tempos = {{0, 500000}, {std::numeric_limits<typename From::unit>::max(), 500000}};
        } else {
            tempos.reserve(score.tempos.size() + 2);
            std::copy(score.tempos.begin(), score.tempos.end(), std::back_inserter(tempos));
            ops::sort_by_time(tempos);
            if(tempos.empty() || tempos[0].time != static_cast<typename From::unit>(0)) {
                tempos.insert(tempos.begin(), Tempo<From>(0, 500000));
            }
            // add a guard at the end
            tempos.emplace_back(std::numeric_limits<typename From::unit>::max(), tempos.back().mspq);
        }
    }
    template<template<class> class T>
    [[nodiscard]] vec<T<To>> time_vec(const vec<T<From>> & data) const {
        const auto self = static_cast<const Converter*>(this);
        // copy and sort origin data
        vec<T<From>> origin(data);  ops::sort_by_time(origin);
        // reserve space for the result
        vec<T<To>> ans; ans.reserve(origin.size());
        // create an iterator for tempos
        auto t_iter = tempos.begin() + 1;
        typename To::unit pivot_to = 0;
        typename From::unit pivot_from = 0;
        double cur_factor = self->get_factor(tempos[0]);
        for(const auto & event : origin) {
            // move to next tempo if necessary
            while(event.time > t_iter->time) {
                pivot_to = self->get_time(t_iter->time, pivot_to, pivot_from, cur_factor);
                pivot_from = t_iter->time;
                cur_factor = self->get_factor(*t_iter);
                ++t_iter;
            }
            ans.emplace_back(self->get_time(event.time, pivot_to, pivot_from, cur_factor), event);
        }
        return ans;
    }

    template<template<class> class T>
     [[nodiscard]] vec<T<To>> duration_vec(const vec<T<From>> & data, typename To::unit min_dur) const {
        const auto self = static_cast<const Converter*>(this);
        min_dur = std::max(min_dur, static_cast<typename To::unit>(0));
        // copy and sort origin data
        vec<T<From>> origin(data);  ops::sort_by_time(origin);
        // reserve space for the result
        vec<T<To>> ans; ans.reserve(origin.size());
        // create an iterator for tempos
        auto t_iter = tempos.begin() + 1;
        typename To::unit pivot_to = 0;
        typename From::unit pivot_from = 0;
        f64 cur_factor = self->get_factor(tempos[0]);
        for(const auto & event : origin) {
            // move to next tempo if necessary
            while(event.time > t_iter->time) {
                pivot_to = self->get_time(t_iter->time, pivot_to, pivot_from, cur_factor);
                pivot_from = t_iter->time;
                cur_factor = self->get_factor(*t_iter);
                ++t_iter;
            }
            ans.emplace_back(
                self->get_time(event.time, pivot_to, pivot_from, cur_factor),
                0, event
            );
        }
        // convert duration according to the end time
        // reserve space for the end times (end(), index)
        vec<std::pair<Tick::unit, u32>> end_times; end_times.reserve(origin.size());
        for(size_t i = 0; i < origin.size(); ++i) {
            end_times.emplace_back(origin[i].end(), i);
        }   // sort them according to the end time
        pdqsort_detail::insertion_sort(end_times.begin(), end_times.end(), [](const auto & a, const auto & b) {
            return a.first < b.first;
        });
        // reset pivot
        pivot_from = 0; pivot_to = 0;
        // reset t_iter
        t_iter = tempos.begin() + 1;
        // reset cur_factor
        cur_factor = self->get_factor(tempos[0]);
        for(const auto & [end, idx] : end_times) {
            while(end > t_iter->time) {
                pivot_to = self->get_time(t_iter->time, pivot_to, pivot_from, cur_factor);
                pivot_from = t_iter->time;
                cur_factor = self->get_factor(*t_iter);
                ++t_iter;
            }
            ans[idx].duration = std::max(
                min_dur,
                self->get_time(end, pivot_to, pivot_from, cur_factor) - ans[idx].time
            );
        }
        return ans;
    }
};

struct Tick2Second: SecondConverter<Tick2Second, Second, Tick> {
    typedef Tick From;
    typedef Second To;

    explicit Tick2Second(const Score<From> & score): SecondConverter{score} {}

    [[nodiscard]] f64 get_factor(const Tempo<From> & tempo) const {
        return static_cast<f64>(tempo.mspq) / 1000000. / tpq;
    }

    [[nodiscard]] To::unit static get_time(
        const From::unit t, const To::unit pivot_to, const From::unit pivot_from, const f64 cur_factor) {
        return pivot_to + static_cast<To::unit>(cur_factor * (t - pivot_from));
    }
};

struct Second2Tick: SecondConverter<Second2Tick, Tick, Second> {
    typedef Second From;
    typedef Tick To;

    explicit Second2Tick(const Score<From> & score): SecondConverter{score} {}

    [[nodiscard]] f64 get_factor(const Tempo<From> & tempo) const {
        return 1000000. * tpq / static_cast<f64>(tempo.mspq);
    }

    [[nodiscard]] To::unit static get_time(
        const From::unit t, const To::unit pivot_to, const From::unit pivot_from, const f64 cur_factor) {
        return pivot_to + static_cast<To::unit>(std::round(cur_factor * (t - pivot_from)));
    }
};

struct Quarter2Second: SecondConverter<Quarter2Second, Second, Quarter> {
    typedef Quarter From;
    typedef Second To;

    explicit Quarter2Second(const Score<From> & score): SecondConverter{score} {}

    [[nodiscard]] f64 get_factor(const Tempo<From> & tempo) const {
        return static_cast<f64>(tempo.mspq) / 1000000.;
    }

    [[nodiscard]] To::unit static get_time(
        const From::unit t, const To::unit pivot_to, const From::unit pivot_from, const f64 cur_factor) {
        return pivot_to + static_cast<To::unit>(cur_factor * (t - pivot_from));
    }
};

struct Second2Quarter: SecondConverter<Second2Quarter, Quarter, Second> {
    typedef Second From;
    typedef Quarter To;

    explicit Second2Quarter(const Score<From> & score): SecondConverter{score} {}

    [[nodiscard]] f64 get_factor(const Tempo<From> & tempo) const {
        return 1000000. / static_cast<f64>(tempo.mspq);
    }

    [[nodiscard]] To::unit static get_time(
        const From::unit t, const To::unit pivot_to, const From::unit pivot_from, const f64 cur_factor) {
        return pivot_to + static_cast<To::unit>(std::round(cur_factor * (t - pivot_from)));
    }
};

} // namespace details

#define IMPLEMENT_CONVERT(To, From)                                                             \
    template<> Score<To> convert<To, From>(const Score<From>& score, To::unit min_dur) {        \
        return details::convertInner<To, From>(score, details::From##2##To(score), min_dur);    \
    }

//                To        From
IMPLEMENT_CONVERT(Tick,     Tick)
IMPLEMENT_CONVERT(Quarter,  Quarter)
IMPLEMENT_CONVERT(Second,   Second)

IMPLEMENT_CONVERT(Tick,     Quarter)
IMPLEMENT_CONVERT(Quarter,  Tick)

IMPLEMENT_CONVERT(Second,   Tick)
IMPLEMENT_CONVERT(Tick,     Second)

IMPLEMENT_CONVERT(Second,   Quarter)
IMPLEMENT_CONVERT(Quarter,  Second)

#undef IMPLEMENT_CONVERT

namespace details {
Score<Tick> resample_inner(const Score<Tick> & score, const i32 tpq, const i32 min_dur) {
    Score<Tick> ans(tpq);

#define CONVERT_TIME(VALUE) \
    static_cast<i32>(std::round(static_cast<double>(tpq * (VALUE)) / static_cast<double>(score.ticks_per_quarter)))

#define RESAMPLE_GENERAL(__COUNT, VEC)                      \
    ans.VEC.reserve(score.VEC.size());                      \
    for(const auto &item: score.VEC)                        \
    ans.VEC.emplace_back(CONVERT_TIME(item.time), item);

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define RESAMPLE_DUR(__COUNT, VEC)                          \
    ans.VEC.reserve(score.VEC.size());                      \
    for(const auto &item: score.VEC)                        \
        ans.VEC.emplace_back(                               \
            CONVERT_TIME(item.time),                        \
            MAX(                                            \
                CONVERT_TIME(item.duration), min_dur        \
            ), item                                         \
       );

    REPEAT_ON(RESAMPLE_GENERAL, time_signatures, key_signatures, tempos, lyrics, markers)
    const size_t track_num = score.tracks.size();
    ans.tracks = vec<Track<Tick>>(track_num);
    for(size_t i = 0; i < track_num; ++i) {
        const auto &track = score.tracks[i];
        auto &new_track = ans.tracks[i];
        new_track.name = track.name;
        new_track.program = track.program;
        new_track.is_drum = track.is_drum;
        REPEAT_ON(RESAMPLE_DUR, tracks[i].notes, tracks[i].pedals)
        REPEAT_ON(RESAMPLE_GENERAL, tracks[i].controls, tracks[i].pitch_bends)
    }

#undef CONVERT_TIME
#undef RESAMPLE_GENERAL
#undef RESAMPLE_DUR
#undef MAX
    return ans;
}
} // namespace details

template<>
Score<Tick> resample(const Score<Quarter> & score, const i32 tpq, const i32 min_dur) {
    return details::resample_inner(convert<Tick>(score), tpq, min_dur);
}

template<>
Score<Tick> resample(const Score<Tick> & score, const i32 tpq, const i32 min_dur) {
    return details::resample_inner(score, tpq, min_dur);
}
template<>
Score<Tick> resample(const Score<Second> & score, const i32 tpq, const i32 min_dur) {
    return details::resample_inner(convert<Tick>(score), tpq, min_dur);
}

} // namespace symusic