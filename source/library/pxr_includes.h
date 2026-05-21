// SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
//
#pragma once

#pragma warning(push)
#pragma warning(disable : 4244) // = Conversion from double to float / int to float
#pragma warning(disable : 4305) // argument truncation from double to float
#pragma warning(disable : 4643) // fwd declarations in namespace std
#pragma warning(disable : 4800) // int to bool
#pragma warning(disable : 4996) // call to std::copy with parameters that may be unsafe
#pragma warning(disable : 4668)
#pragma warning(disable : 4946)
#pragma warning(disable : 4267)
// https://answers.unrealengine.com/questions/607946/anonymous-union-with-none-trivial-type.html
#pragma warning(disable : 4582) // PcpMapFunction::_Data::localPairs': constructor is not implicitly called
#pragma warning(disable : 4583) // PcpMapFunction::_Data::localPairs': destructor is not implicitly called
#pragma push_macro("check")
#pragma push_macro("__MSVC_RUNTIME_CHECKS")
#undef check
#undef __MSVC_RUNTIME_CHECKS

#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/matrix2f.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/quaternion.h>
#include <pxr/base/gf/range3f.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/transform.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/tf/atomicOfstreamWrapper.h>
#include <pxr/base/tf/declarePtrs.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/enum.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/stringUtils.h>
#if PXR_MINOR_VERSION >= 24
#    include <pxr/base/tf/unicodeUtils.h>
#endif
#include <pxr/base/vt/array.h>
#include <pxr/pxr.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/pcp/layerStack.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/proxyTypes.h>
#include <pxr/usd/sdf/relationshipSpec.h>

#if PXR_MINOR_VERSION < 25 || (PXR_MINOR_VERSION == 25 && PXR_PATCH_VERSION < 11)
#    include <pxr/usd/sdf/textFileFormat.h>
#    include <pxr/usd/usd/usdFileFormat.h>
#    include <pxr/usd/usd/usdaFileFormat.h>
#    include <pxr/usd/usd/usdcFileFormat.h>
#    include <pxr/usd/usd/usdzFileFormat.h>
#else
#    include <pxr/usd/sdf/usdFileFormat.h>
#    include <pxr/usd/sdf/usdaFileFormat.h>
#    include <pxr/usd/sdf/usdcFileFormat.h>
#    include <pxr/usd/sdf/usdzFileFormat.h>
#endif

#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdf/variantSetSpec.h>
#include <pxr/usd/sdf/variantSpec.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/notice.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/stageCache.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/geometryLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdSkel/animation.h>
#include <pxr/usd/usdSkel/bakeSkinning.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/blendShape.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/tokens.h>
#include <pxr/usd/usdSkel/topology.h>
#include <pxr/usd/usdSkel/utils.h>
#include <pxr/usd/usdUtils/authoring.h>
#include <pxr/usd/usdUtils/pipeline.h>
#include <pxr/usd/usdUtils/stageCache.h>

#pragma pop_macro("check")
#pragma pop_macro("__MSVC_RUNTIME_CHECKS")
#pragma warning(pop)
