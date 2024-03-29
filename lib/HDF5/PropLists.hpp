/*
 * Copyright (c) 2010-2012 Steffen Kieß
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HDF5_PROPLISTS_HPP_INCLUDED
#define HDF5_PROPLISTS_HPP_INCLUDED

// HDF5 property lists

#include <Core/Assert.hpp>
#include <Core/Util.hpp>

#include <hdf5.h>

#include <HDF5/PropList.hpp>

namespace HDF5 {
#define PLIST(ClassName, ParentClass, cls)                                   \
  class ClassName : public ParentClass {                                     \
    void checkType() const {                                                 \
      if (!isValid()) return;                                                \
      if (!isA(cls))                                                         \
        ABORT_MSG("Not a " #ClassName " (" #cls ") property list");          \
    }                                                                        \
                                                                             \
   public:                                                                   \
    ClassName() {}                                                           \
                                                                             \
    explicit ClassName(const IdComponent& value) : ParentClass(value) {      \
      checkType();                                                           \
    }                                                                        \
                                                                             \
    /* This constructor takes ownership of the object refered to by value */ \
    explicit ClassName(hid_t value) : ParentClass(value) { checkType(); }    \
                                                                             \
    static ClassName create() {                                              \
      Exception::check("H5open", H5open());                                  \
      return (ClassName)PropList::create(cls);                               \
    }                                                                        \
  };

// List generated by GetConsts
PLIST(ObjectCreatePropList, PropList, H5P_OBJECT_CREATE)
PLIST(FileAccessPropList, PropList, H5P_FILE_ACCESS)
PLIST(DataSetCreatePropList, ObjectCreatePropList, H5P_DATASET_CREATE)
PLIST(DataTransferPropList, PropList, H5P_DATASET_XFER)
PLIST(FileMountPropList, PropList, H5P_FILE_MOUNT)
PLIST(GroupCreatePropList, ObjectCreatePropList, H5P_GROUP_CREATE)
PLIST(DataTypeCreatePropList, ObjectCreatePropList, H5P_DATATYPE_CREATE)
PLIST(StringCreatePropList, PropList, H5P_STRING_CREATE)
PLIST(AttributeCreatePropList, StringCreatePropList, H5P_ATTRIBUTE_CREATE)
PLIST(ObjectCopyPropList, PropList, H5P_OBJECT_COPY)
PLIST(LinkCreatePropList, StringCreatePropList, H5P_LINK_CREATE)
PLIST(LinkAccessPropList, PropList, H5P_LINK_ACCESS)
PLIST(FileCreatePropList, GroupCreatePropList, H5P_FILE_CREATE)
PLIST(DataSetAccessPropList, LinkAccessPropList, H5P_DATASET_ACCESS)
PLIST(GroupAccessPropList, LinkAccessPropList, H5P_GROUP_ACCESS)
PLIST(DataTypeAccessPropList, LinkAccessPropList, H5P_DATATYPE_ACCESS)

#undef PLIST
}  // namespace HDF5

#endif  // !HDF5_PROPLISTS_HPP_INCLUDED
