/***************************************************************************
 * Copyright 1998-2018 by authors (see AUTHORS.txt)                        *
 *                                                                         *
 *   This file is part of LuxCoreRender.                                   *
 *                                                                         *
 * Licensed under the Apache License, Version 2.0 (the "License");         *
 * you may not use this file except in compliance with the License.        *
 * You may obtain a copy of the License at                                 *
 *                                                                         *
 *     http://www.apache.org/licenses/LICENSE-2.0                          *
 *                                                                         *
 * Unless required by applicable law or agreed to in writing, software     *
 * distributed under the License is distributed on an "AS IS" BASIS,       *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
 * See the License for the specific language governing permissions and     *
 * limitations under the License.                                          *
 ***************************************************************************/

#include "slg/bsdf/bsdf.h"
#include "slg/scene/scene.h"
#include "slg/lights/constantinfinitelight.h"
#include "slg/lights/visibility/envlightvisibility.h"

using namespace std;
using namespace luxrays;
using namespace slg;

//------------------------------------------------------------------------------
// ConstantInfiniteLight
//------------------------------------------------------------------------------

ConstantInfiniteLight::ConstantInfiniteLight() : color(1.f), visibilityDistribution(nullptr),
		visibilityMapCache(nullptr) {
}

ConstantInfiniteLight::~ConstantInfiniteLight() {
	delete visibilityDistribution;
	delete visibilityMapCache;
}

void ConstantInfiniteLight::GetPreprocessedData(const luxrays::Distribution2D **visibilityDist,
		const EnvLightVisibilityCache **elvc) const {
	if (visibilityDist)
		*visibilityDist = visibilityDistribution;
	if (elvc)
		*elvc = visibilityMapCache;
}

float ConstantInfiniteLight::GetPower(const Scene &scene) const {
	const float envRadius = GetEnvRadius(scene);

	return gain.Y() * (4.f * M_PI * M_PI * envRadius * envRadius) *
		color.Y();
}

Spectrum ConstantInfiniteLight::GetRadiance(const Scene &scene,
		const BSDF *bsdf, const Vector &dir, float *directPdfA, float *emissionPdfW) const {
	const float envRadius = GetEnvRadius(scene);

	if (visibilityDistribution || visibilityMapCache) {
		const Vector w = -dir;
		float u, v, latLongMappingPdf;
		ToLatLongMapping(w, &u, &v, &latLongMappingPdf);
		if (latLongMappingPdf == 0.f)
			return Spectrum();

		if (directPdfA) {
			if (!bsdf)
				*directPdfA = 0.f;
			else if (visibilityMapCache && visibilityMapCache->IsCacheEnabled(*bsdf)) {
				const Distribution2D *cacheDist = visibilityMapCache->GetVisibilityMap(*bsdf);
				if (cacheDist) {
					const float cacheDistPdf = cacheDist->Pdf(u, v);

					*directPdfA = cacheDistPdf * latLongMappingPdf;
				} else
					*directPdfA = 0.f;
			} else if (visibilityDistribution) {
				const float distPdf = visibilityDistribution->Pdf(u, v);
				*directPdfA = distPdf * latLongMappingPdf;
			} else
				*directPdfA = 0.f;
		}

		if (emissionPdfW) {
			if (visibilityDistribution) {
				const float distPdf = visibilityDistribution->Pdf(u, v);
				*emissionPdfW = distPdf * latLongMappingPdf / (M_PI * envRadius * envRadius);
			} else
				*emissionPdfW = UniformSpherePdf() / (M_PI * envRadius * envRadius);
		}
	} else {
		if (directPdfA)
			*directPdfA = bsdf ? UniformSpherePdf() : 0.f;

		if (emissionPdfW)
			*emissionPdfW = UniformSpherePdf() / (M_PI * envRadius * envRadius);
	}

	return gain * color;
}

Spectrum ConstantInfiniteLight::Emit(const Scene &scene,
		const float u0, const float u1, const float u2, const float u3, const float passThroughEvent,
		Point *orig, Vector *dir,
		float *emissionPdfW, float *directPdfA, float *cosThetaAtLight) const {
	const Point worldCenter = scene.dataSet->GetBSphere().center;
	const float envRadius = GetEnvRadius(scene);

	if (visibilityDistribution) {
		// Choose p1 on scene bounding sphere according importance sampling
		float uv[2];
		float distPdf;
		visibilityDistribution->SampleContinuous(u0, u1, uv, &distPdf);
		if (distPdf == 0.f)
			return Spectrum();
		
		Vector v;
		float latLongMappingPdf;
		FromLatLongMapping(uv[0], uv[1], &v, &latLongMappingPdf);
		if (latLongMappingPdf == 0.f)
			return Spectrum();

		// Compute InfiniteLight ray weight
		*emissionPdfW = distPdf * latLongMappingPdf / (M_PI * envRadius * envRadius);

		if (directPdfA)
			*directPdfA = distPdf * latLongMappingPdf;
	} else {
		// Compute InfiniteLight ray weight
		*emissionPdfW = UniformSpherePdf() / (M_PI * envRadius * envRadius);

		if (directPdfA)
			*directPdfA = UniformSpherePdf();
	}

	// Choose p1 on scene bounding sphere
	Point p1 = worldCenter + envRadius * UniformSampleSphere(u0, u1);

	// Choose p2 on scene bounding sphere
	Point p2 = worldCenter + envRadius * UniformSampleSphere(u2, u3);

	// Construct ray between p1 and p2
	*orig = p1;
	*dir = Normalize((p2 - p1));

	if (cosThetaAtLight)
		*cosThetaAtLight = Dot(Normalize(worldCenter -  p1), *dir);

	return GetRadiance(scene, nullptr, *dir);
}

Spectrum ConstantInfiniteLight::Illuminate(const Scene &scene, const BSDF &bsdf,
		const float u0, const float u1, const float passThroughEvent,
        Vector *dir, float *distance, float *directPdfW,
		float *emissionPdfW, float *cosThetaAtLight) const {
	const Point &p = bsdf.hitPoint.p;
	const float envRadius = GetEnvRadius(scene);

	if (visibilityDistribution || visibilityMapCache) {
		float uv[2];
		float distPdf;
		
		if (visibilityMapCache && visibilityMapCache->IsCacheEnabled(bsdf)) {
			const Distribution2D *cacheDist = visibilityMapCache->GetVisibilityMap(bsdf);
			if (cacheDist)
				cacheDist->SampleContinuous(u0, u1, uv, &distPdf);
			else
				return Spectrum();
		} else if (visibilityDistribution)
			visibilityDistribution->SampleContinuous(u0, u1, uv, &distPdf);
		else
			return Spectrum();
			
		if (distPdf == 0.f)
			return Spectrum();

		float latLongMappingPdf;
		FromLatLongMapping(uv[0], uv[1], dir, &latLongMappingPdf);
		if (latLongMappingPdf == 0.f)
			return Spectrum();

		*directPdfW = distPdf * latLongMappingPdf;

		if (emissionPdfW)
			*emissionPdfW = distPdf * latLongMappingPdf / (M_PI * envRadius * envRadius);
	} else {
		*dir = UniformSampleSphere(u0, u1);

		*directPdfW = UniformSpherePdf();

		if (emissionPdfW)
			*emissionPdfW = UniformSpherePdf() / (M_PI * envRadius * envRadius);
	}

	const Point worldCenter = scene.dataSet->GetBSphere().center;

	const Vector toCenter(worldCenter - p);
	const float centerDistance = Dot(toCenter, toCenter);
	const float approach = Dot(toCenter, *dir);
	*distance = approach + sqrtf(Max(0.f, envRadius * envRadius -
		centerDistance + approach * approach));

	const Point emisPoint(p + (*distance) * (*dir));
	const Normal emisNormal(Normalize(worldCenter - emisPoint));

	const float cosAtLight = Dot(emisNormal, -(*dir));
	if (cosAtLight < DEFAULT_COS_EPSILON_STATIC)
		return Spectrum();
	if (cosThetaAtLight)
		*cosThetaAtLight = cosAtLight;

	return gain * color;
}

UV ConstantInfiniteLight::GetEnvUV(const luxrays::Vector &dir) const {
	UV uv;
	const Vector w = -dir;
	ToLatLongMapping(w, &uv.u, &uv.v);
	
	return uv;
}

void ConstantInfiniteLight::UpdateVisibilityMap(const Scene *scene, const bool useRTMode) {
	if (useRTMode) {
		delete visibilityMapCache;
		visibilityMapCache = nullptr;

		delete visibilityDistribution;
		visibilityDistribution = nullptr;

		return;
	}

	if (useVisibilityMapCache) {
		delete visibilityMapCache;
		visibilityMapCache = nullptr;

		visibilityMapCache = new EnvLightVisibilityCache(scene, this, nullptr, visibilityMapCacheParams);		
		visibilityMapCache->Build();
	} else if (useVisibilityMap) {
		EnvLightVisibility envLightVisibilityMapBuilder(scene, this,
				NULL, false,
				visibilityMapWidth, visibilityMapHeight,
				visibilityMapSamples, visibilityMapMaxDepth);
		
		Distribution2D *newDist = envLightVisibilityMapBuilder.Build();
		if (newDist) {
			delete visibilityDistribution;
			visibilityDistribution = newDist;
		}
	}
}

Properties ConstantInfiniteLight::ToProperties(const ImageMapCache &imgMapCache, const bool useRealFileName) const {
	const string prefix = "scene.lights." + GetName();
	Properties props = EnvLightSource::ToProperties(imgMapCache, useRealFileName);

	props.Set(Property(prefix + ".type")("constantinfinite"));
	props.Set(Property(prefix + ".color")(color));
	props.Set(Property(prefix + ".visibilitymap.enable")(useVisibilityMap));
	props.Set(Property(prefix + ".visibilitymap.width")(visibilityMapWidth));
	props.Set(Property(prefix + ".visibilitymap.height")(visibilityMapHeight));
	props.Set(Property(prefix + ".visibilitymap.samples")(visibilityMapSamples));
	props.Set(Property(prefix + ".visibilitymap.maxdepth")(visibilityMapMaxDepth));

	props.Set(Property(prefix + ".visibilitymapcache.enable")(useVisibilityMapCache));
	if (useVisibilityMapCache)
		props << EnvLightVisibilityCache::Params2Props(prefix, visibilityMapCacheParams);

	return props;
}
