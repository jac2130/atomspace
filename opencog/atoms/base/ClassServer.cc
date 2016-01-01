/*
 * opencog/atoms/base/ClassServer.cc
 *
 * Copyright (C) 2002-2007 Novamente LLC
 * Copyright (C) 2008 by OpenCog Foundation
 * Copyright (C) 2009 Linas Vepstas <linasvepstas@gmail.com>
 * All Rights Reserved
 *
 * Written by Thiago Maia <thiago@vettatech.com>
 *            Andre Senna <senna@vettalabs.com>
 *            Gustavo Gama <gama@vettalabs.com>
 *            Linas Vepstas <linasvepstas@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "ClassServer.h"

#include <exception>
#include <boost/bind.hpp>

#include <opencog/atoms/base/atom_types.h>
#include "types.h"
#include <opencog/util/Logger.h>

#include "opencog/atoms/base/atom_types.definitions"

//#define DPRINTF printf
#define DPRINTF(...)

using namespace opencog;

ClassServer::ClassServer(void)
{
    logger().info("Initializing ClassServer");
    nTypes = 0;
    // autogenerated code to initialize all atom types defined in
    // atom_types.script file:
    #include "opencog/atoms/base/atom_types.inheritance"
}

ClassServer* ClassServer::createInstance(void)
{
    return new ClassServer();
}

Type ClassServer::addType(const Type parent, const std::string& name)
{
    // Check if a type with this name already exists. If it does, then
    // the second and subsequent calls are to be interpreted as defining
    // multiple inheritance for this type.  A real-life example is the
    // GroundedSchemeNode, which inherits from several types.
    Type type = getType(name);
    if (type != NOTYPE) {
        std::lock_guard<std::mutex> l(type_mutex);
        DPRINTF("Type \"%s\" has already been added (%d)\n", name.c_str(), type);
        inheritanceMap[parent][type] = true;
        setParentRecursively(parent, type);
        return type;
    }

    std::unique_lock<std::mutex> l(type_mutex);
    // Assign type code and increment type counter.
    type = nTypes++;

    // Resize inheritanceMap container.
    inheritanceMap.resize(nTypes);
    recursiveMap.resize(nTypes);

    std::for_each(inheritanceMap.begin(), inheritanceMap.end(),
          boost::bind(&std::vector<bool>::resize, _1, nTypes, false));

    std::for_each(recursiveMap.begin(), recursiveMap.end(),
          boost::bind(&std::vector<bool>::resize, _1, nTypes, false));

    inheritanceMap[type][type]   = true;
    inheritanceMap[parent][type] = true;
    recursiveMap[type][type]     = true;
    setParentRecursively(parent, type);
    name2CodeMap[name]           = type;
    code2NameMap[type]           = &(name2CodeMap.find(name)->first);

    // unlock mutex before sending signal which could call
    l.unlock();

    // Emit add type signal.
    _addTypeSignal(type);

    return type;
}

void ClassServer::setParentRecursively(Type parent, Type type)
{
    recursiveMap[parent][type] = true;
    for (Type i = 0; i < nTypes; ++i) {
        if ((recursiveMap[i][parent]) && (i != parent)) {
            setParentRecursively(i, type);
        }
    }
}

boost::signals2::signal<void (Type)>& ClassServer::addTypeSignal()
{
    return _addTypeSignal;
}

Type ClassServer::getNumberOfClasses()
{
    return nTypes;
}

bool ClassServer::isA_non_recursive(Type type, Type parent)
{
    std::lock_guard<std::mutex> l(type_mutex);
    if ((type >= nTypes) || (parent >= nTypes)) return false;
    return inheritanceMap[parent][type];
}

bool ClassServer::isDefined(const std::string& typeName)
{
    std::lock_guard<std::mutex> l(type_mutex);
    return name2CodeMap.find(typeName) != name2CodeMap.end();
}

Type ClassServer::getType(const std::string& typeName)
{
    std::lock_guard<std::mutex> l(type_mutex);
    std::unordered_map<std::string, Type>::iterator it = name2CodeMap.find(typeName);
    if (it == name2CodeMap.end()) {
        return NOTYPE;
    }
    return it->second;
}

const std::string& ClassServer::getTypeName(Type type)
{
    static std::string nullString = "*** Unknown Type! ***";

    std::lock_guard<std::mutex> l(type_mutex);
    std::unordered_map<Type, const std::string*>::iterator it;
    if ((it = code2NameMap.find(type)) != code2NameMap.end())
        return *(it->second);
    return nullString;
}

ClassServer& opencog::classserver(ClassServerFactory* factory)
{
    static std::unique_ptr<ClassServer> instance((*factory)());
    return *instance;
}

// This runs when the shared lib is loaded.  We have to make
// sure that all of the core types are initialized before
// anything else happens, as otherwise weird symptoms manifest.
static __attribute__ ((constructor)) void init(void)
{
	classserver();
}
