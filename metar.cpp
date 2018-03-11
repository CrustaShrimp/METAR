//
// Copyright (c) 2018 James A. Chappell
//
// METAR decoder
//

#include "metar.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <limits.h>
#include <float.h>

const int Metar::_INTEGER_UNDEFINED = INT_MIN;
const double Metar::_DOUBLE_UNDEFINED = DBL_MAX;
const unsigned int Metar::_MAX_CLOUD_LAYERS = 3;

static const char *WIND_SPEED_KT = "KT";
static const char *WIND_SPEED_MPS = "MPS";
static const char *WIND_SPEED_KPH = "KPH";

static const char *VIS_UNITS_M = "M";
static const char *VIS_UNITS_SM = "SM";

static const char *sky_conditions[] =
{
  "SKC",
  "CLR",
  "NSC",
  "FEW",
  "SCT",
  "BKN",
  "OVC"
};
static const auto NUM_LAYERS =
  sizeof(sky_conditions) / sizeof(sky_conditions[0]);

static const char *cloud_types[] =
{
  "TCU",
  "CB",
  "ACC" 
};
static const auto NUM_CLOUDS =
  sizeof(cloud_types) / sizeof(cloud_types[0]);

class SkyConditionImpl : public Metar::SkyCondition
{
public:
  SkyConditionImpl(const char *c = nullptr, int a = INT_MIN,
                   const char *t = nullptr)
    : _cond(c)
    , _alt(a)
    , _type(t)
  {
  }

  SkyConditionImpl(const SkyConditionImpl&) = default;
  
  SkyConditionImpl& operator=(const SkyConditionImpl&) = default;

  virtual const char *Condition() const { return _cond; } 
  virtual int Altitude() const { return _alt; }
  virtual bool hasAltitude() const { return _alt != INT_MIN; }
  virtual const char *CloudType() const { return _type; }
  virtual bool hasCloudType() const { return _type != nullptr; }

private:
    const char *_cond;
    int _alt;
    const char *_type;
};

Metar::Metar()
  : _day(_INTEGER_UNDEFINED)
  , _hour(_INTEGER_UNDEFINED)
  , _min(_INTEGER_UNDEFINED)
  , _wind_dir(_INTEGER_UNDEFINED) 
  , _wind_spd(_INTEGER_UNDEFINED)
  , _gust(_INTEGER_UNDEFINED)
  , _wind_speed_units(nullptr)
  , _min_wind_dir(_INTEGER_UNDEFINED)
  , _max_wind_dir(_INTEGER_UNDEFINED)
  , _vrb(false)
  , _vis(_DOUBLE_UNDEFINED)
  , _vis_units(nullptr)
  , _vis_lt(false)
  , _cavok(false)
  , _num_layers(0)
  , _vert_vis(_INTEGER_UNDEFINED)
  , _temp(_INTEGER_UNDEFINED) 
  , _dew(_INTEGER_UNDEFINED)
  , _altimeterA(_DOUBLE_UNDEFINED)
  , _altimeterQ(_INTEGER_UNDEFINED)
  , _slp(_DOUBLE_UNDEFINED)
  , _ftemp(_DOUBLE_UNDEFINED) 
  , _fdew(_DOUBLE_UNDEFINED)
  , _previous_element(nullptr)
{
  _metar[0] = '\0';
  _icao[0] = '\0';

  _layers = new SkyCondition*[_MAX_CLOUD_LAYERS]; 
}

Metar::Metar(const char *metar_str) : Metar()
{
  parse(metar_str);
}

Metar::Metar(char *metar_str) : Metar()
{
  parse(metar_str);
}

Metar::~Metar()
{
  for (size_t i = 0 ; i < _num_layers ; i++)
  {
    delete _layers[i];
  }
  delete[] _layers;
}

static inline bool match_character(const char p, const char c)
{
  switch(p)
  {
    case '#':
      if (!isdigit(c)) return false;
      break;

    case '$':
      if (!isalpha(c)) return false;
      break;

    default:
      if (p != c) return false;
      break;
  }

  return true;
}

static bool match(const char *pattern, const char *str, size_t len)
{
  for (size_t i = 0 ; i < len ; i++)
  {
    if (!match_character(pattern[i], str[i]))
      return false;
  }

  return true;
}

static inline bool match(const char *pattern, const char *str)
{
  size_t pattern_len = strlen(pattern);
  if (str && (pattern_len == strlen(str)))
  {
    return match(pattern, str, pattern_len);
  }

  return false;
}

static inline bool starts_with(const char *pattern, const char *str)
{
  size_t pattern_len = strlen(pattern);
  if (str && (pattern_len <= strlen(str)))
  {
    return match(pattern, str, pattern_len);
  }

  return false;
}  

static inline bool is_metar(const char *str)
{
  return !strcmp(str, "METAR") || !strcmp(str, "SPECI");
}

static inline bool is_icao(const char *str)
{
  return match("$$$$", str);
}

static inline bool is_ot(const char *str)
{
  return match("######Z", str);
}

static inline bool is_wind(const char *str)
{
  return starts_with("#####", str) 
      || starts_with("#####G##", str) 
      || starts_with("######G###", str)
      || starts_with("VRB", str);
}

static inline bool is_wind_var(const char *str)
{
  return match("###V###", str);
}

static inline bool is_vis(const char *str)
{
  if (!strcmp(str, "CAVOK"))
    return true;

  const char *p = strstr(str, VIS_UNITS_SM);
  if (!p)
  {  
    return match("####", str);
  }
 
  auto len = strlen(str); 
  if ((str + len - p) == 2)
  {
    if (!isdigit(str[0]) && (str[0] != 'M')) return false;
    for (size_t i = 1 ; i < len - 2 ; i++)
    {
      if (!isdigit(str[i]) && str[i] != '/') return false;
    }

    return true;
  }

  return false;
}

static inline bool is_cloud_layer(const char *str)
{
  for (size_t i = 0 ; i < NUM_LAYERS ; i++)
  {
    if (starts_with(sky_conditions[i], str))
        return true;
  }

  return false;
}

static inline bool is_vert_vis(const char *str)
{
  return match("VV###", str);
}

static inline bool is_temp(const char *str)
{
  return match("##/##", str) 
    || match("##/M##", str) 
    || match("M##/M##", str)
    || match("##/", str)
    || match("M##/", str);
}

static inline bool is_altA(const char *str)
{
  return match("A####", str);
}

static inline bool is_altQ(const char *str)
{
  return match("Q####", str);
}

static inline bool is_slp(const char *str)
{
  return match("SLP###", str);
}

static inline bool is_tempNA(const char *str)
{
  return match("T########", str);
}

void Metar::parse(const char *metar_str)
{
  char *metar_dup = strdup(metar_str);
  parse(metar_dup);
  free(metar_dup);
}

void Metar::parse(char *metar_str)
{
  char *el = strtok(metar_str, " ");
  while (el)
  {
    if (!hasMETAR() && is_metar(el))
    {
      parse_metar(el);
    }
    else if (!hasICAO() && is_icao(el))
    {
      parse_icao(el);
    }
    else if (!hasMinute() && is_ot(el))
    {
      parse_ot(el);
    }
    else if (!hasWindSpeed() && is_wind(el))
    {
      parse_wind(el);
    }
    else if (!hasMinWindDirection() && is_wind_var(el))
    {
      parse_wind_var(el);
    }
    else if (!hasVisibility() && !_cavok && is_vis(el))
    {
      parse_vis(el);
    }
    else if ((_num_layers < _MAX_CLOUD_LAYERS) && is_cloud_layer(el))
    {
      parse_cloud_layer(el);
    } 
    else if (!hasVerticalVisibility() && is_vert_vis(el))
    {
      parse_vert_vis(el);
    }
    else if (!hasTemperature() && is_temp(el))
    {
      parse_temp(el);
    } 
    else if (!hasAltimeterA() && is_altA(el))
    {
      parse_alt(el);
    }
    else if (!hasAltimeterQ() && is_altQ(el))
    {
      parse_alt(el);
    }
    else if (!hasSeaLevelPressure() && is_slp(el))
    {
      parse_slp(el);
    }
    else if (!hasTemperatureNA() && is_tempNA(el))
    {
      parse_tempNA(el);
    }

    _previous_element = el;

    el = strtok(nullptr, " ");
  }
}

void Metar::parse_metar(const char *str)
{
  strcpy(_metar, str);
}

void Metar::parse_icao(const char *str)
{
  strcpy(_icao, str);
}

void Metar::parse_ot(const char *str)
{
  char val[3];

  strncpy(val, str, 2);
  val[2] = '\0';
  _day = atoi(val);

  strncpy(val, str + 2, 2);
  val[2] = '\0';
  _hour = atoi(val);

  strcpy(val, str + 4);
  _min = atoi(val);
}

void Metar::parse_wind(const char *str)
{
  if (strstr(str, WIND_SPEED_MPS))
  {
    _wind_speed_units = WIND_SPEED_MPS;
  }
  else if (strstr(str, WIND_SPEED_KPH))
  {
    _wind_speed_units = WIND_SPEED_KPH;
  }
  else
  {
    _wind_speed_units = WIND_SPEED_KT;
  }

  char val[4];

  if (!strstr(str, "VRB"))
  {
    strncpy(val, str, 3);
    val[3] = '\0';
    _wind_dir = atoi(val);
  }
  else
  {
    _vrb = true;
  }
 
  strncpy(val, str + 3, 3);
  val[3] = '\0';
  
  _wind_spd = atoi(val);

  const char *g = strstr(str, "G");
  if (g)
  {
    strncpy(val, g + 1, 3);
    val[3] = '\0';
    _gust = atoi(val);
  } 
}

void Metar::parse_wind_var(const char *str)
{
  char val[4];

  strncpy(val, str, 3);
  val[3] = '\0';
  _min_wind_dir = atoi(val);

  strcpy(val, str + 4);
  _max_wind_dir = atoi(val);
}

void Metar::parse_vis(const char *str)
{
  if (!strcmp(str, "CAVOK"))
  {
    _cavok = true;
    return;
  }

  const char *u = strstr(str, VIS_UNITS_SM);
  if (!u)
  {
    _vis = atof(str);
    _vis_units = VIS_UNITS_M;
  }
  else
  {
    const char *p = strstr(str, "/");
    if (!p)
    {
      _vis = atof(str);
    }
    else
    {
      char val[4];
      auto len = p - str;
      
      if (str[0] == 'M')
      {
        strncpy(val, str + 1, len);
        _vis_lt = true;
      }
      else
      {
        strncpy(val, str, len);
      }
      val[len] = '\0';
      double numerator = atof(val);

      len = u - p;
      strncpy(val, p + 1, len);
      val[len] = '\0';
      double denominator = atof(val);

      _vis = numerator / denominator;
      if (match("#", _previous_element))
      {
        _vis += atof(_previous_element);
      }
    }
    _vis_units = VIS_UNITS_SM;
  }
}

void Metar::parse_cloud_layer(const char *str)
{
  for (size_t i = 0 ; i < NUM_LAYERS ; i++)
  {
    if (starts_with(sky_conditions[i], str))
    {
      if (str[3] == '\0')
      {
        _layers[_num_layers++] = new SkyConditionImpl(sky_conditions[i]);
      }
      else if (str[6] == '\0')
      {
        _layers[_num_layers++] =
            new SkyConditionImpl(sky_conditions[i], atoi(str + 3) * 100);
      }
      else
      {
        const char *c =nullptr;
        for (size_t j = 0 ; j < NUM_CLOUDS ; j++)
        {
          if (!strcmp(str + 6, cloud_types[j]))
          {
            c = cloud_types[j];
            break;
          }
        } 
        _layers[_num_layers++] =
            new SkyConditionImpl(sky_conditions[i], 
                                 atoi(str + 3) * 100,
                                 c);
      }
    }
  }
}

void Metar::parse_vert_vis(const char *str)
{
  _vert_vis = atoi(str + 2) * 100;
}

static inline int temp(char *val)
{
  if (val[0] == 'M') val[0] = '-';
  return atoi(val);
}

void Metar::parse_temp(const char *str)
{
  char val[4];

  const char *p = strstr(str, "/");
  strncpy(val, str, p - str);

  val[p - str] = '\0';
  _temp = temp(val);

  if (*(p + 1) != '\0')
  {
    strcpy(val, p + 1);
    _dew = temp(val);
  }
}

void Metar::parse_alt(const char *str)
{
  int val = atoi(str + 1);
  if (str[0] == 'Q')
    _altimeterQ = val;
  else
    _altimeterA = static_cast<double>(val) / 100.0;
}

void Metar::parse_slp(const char *str)
{
  _slp = (atof(str + 3) / 10.0) + 1000.0;
}

static inline double tempNA(char *val)
{
  if (val[0] == '1') val[0] = '-';
  return atof(val) / 10.0;
}

void Metar::parse_tempNA(const char *str)
{
  char val[5];

  strncpy(val, str + 1, 4);
  val[4] = '\0';
  _ftemp = tempNA(val);

  strcpy(val, str + 5);
  _fdew = tempNA(val);
}
