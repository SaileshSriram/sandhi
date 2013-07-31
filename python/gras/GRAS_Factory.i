// Copyright (C) by Josh Blum. See LICENSE.txt for licensing information.

%include <gras/exception.i>
%module (package="gras") GRAS_Factory

%{
#include <gras/factory.hpp>
%}

namespace gras
{
    %ignore Factory::register_factory;
    %ignore Factory::make;
}

%newobject gras::Factory::_handle_make;

////////////////////////////////////////////////////////////////////////
// Export swig element comprehension
////////////////////////////////////////////////////////////////////////
%include <std_string.i>
%import <PMC/PMC.i>
%import <gras/element.i>
%include <gras/gras.hpp>
%include <gras/factory.hpp>

////////////////////////////////////////////////////////////////////////
// Create python make method for the factory
////////////////////////////////////////////////////////////////////////
%pythoncode %{
#TODO we need to register this into the real factory
_py_factory = dict()

def register_factory(path, fcn):
    #TODO we need to register this into the real factory
    _py_factory[path] = fcn

def make(path, *args, **kwargs):

    #first try the local to python py factory #TODO real factory
    if path in _py_factory: return _py_factory[path](*args, **kwargs)

    from PMC import PMC_M
    pmcargs = PMC_M(list(args))
    return Factory._handle_make(path, pmcargs)

%}
