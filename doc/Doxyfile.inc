# This file provides overrides to the Doxyfile configuration

PROJECT_NAME           = lib_vga
PROJECT_BRIEF          = "VGA display controller"

INPUT                  = ../lib_vga/api

PREDEFINED             = C_API= EXTERN_C= slave= __DOXYGEN__=1 DECLARE_JOB(a,b)=void a b;
