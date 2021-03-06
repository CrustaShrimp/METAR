//
// Copyright (c) 2018 James A. Chappell
//
// Weather utilities
//

#ifndef STORAGE_B_WEATHER_UTILS_H_
#define STORAGE_B_WEATHER_UTILS_H_

#include "defines.h"

namespace Storage_B 
{
  namespace Weather
  {
    class Utils
    {
    public:
      static double Humidity(double t, double td);
      static double WindChill(double temp, double wind_speed);
      static double HeatIndex(double temp, double humidity,
                              bool celsius_flg = false);

      Utils() = delete;
      Utils(const Utils&) = delete;
      Utils& operator=(const Utils&) = delete;
      ~Utils() = default;
    };
  }
}

#endif
