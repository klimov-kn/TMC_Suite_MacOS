// gl/glu.h — переходник к системному заголовку GLU (см. gl/gl.h рядом).
//
// В macOS GLU лежит в той же рамке OpenGL: <OpenGL/glu.h>.

#pragma once

#ifdef __APPLE__
#  ifndef GL_SILENCE_DEPRECATION
#    define GL_SILENCE_DEPRECATION 1
#  endif
#  include <OpenGL/glu.h>
#else
#  include_next <GL/glu.h>
#endif
