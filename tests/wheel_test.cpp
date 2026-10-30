#include "wheel.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

int main() {
	Wheel w;
	for (int32_t i = 0; i < 17; ++i) w.setPacejka(i, static_cast<float>(i + 1));
	const auto before = w.pacejka;
	w.setPacejka(-1, 100); w.setPacejka(17, 100);
	w.setPacejka(std::numeric_limits<int32_t>::min(), 100);
	w.setPacejka(std::numeric_limits<int32_t>::max(), 100);
	CHECK(w.pacejka == before);
	w.setSliction(20); w.setStiffness(21); w.setMaxLoad(22);
	w.setFrictn_x(23); w.setLoadSmooth(24);
	CHECK(w.pacejka[0] == 20 && w.pacejka[4] == 21 && w.pacejka[11] == 22);
	CHECK(w.pacejka[12] == 23 && w.pacejka[14] == 24);
	w.setPacejka(0, 30); w.setPacejka(4, 31); w.setPacejka(11, 32);
	w.setPacejka(12, 33); w.setPacejka(14, 34);
	CHECK(w.pacejka[0] == 30 && w.pacejka[4] == 31 && w.pacejka[11] == 32);
	CHECK(w.pacejka[12] == 33 && w.pacejka[14] == 34);
	w.frictionScale = 0.5f;
	w.setFriction(4.0f);
	CHECK(w.pacejka[2] == 4 && w.friction64 == 4 && w.friction68 == 4);
	CHECK(std::fabs(w.friction6c - 3.4f) < 0.00001f && w.friction70 == w.friction6c);
	w.setPacejka(2, 9);
	CHECK(w.pacejka[2] == 9 && w.friction64 == 4 && w.friction6c != 9);
	w.frictionScale = 1.0f;
	w.setFriction(1.0f);
	CHECK(w.pacejka[2] == 1 && w.friction6c == 0x1.9999ap-4f && w.friction70 == w.friction6c);
	w.frictionScale = 2.0f;
	w.setFriction(1.0f);
	CHECK(w.friction6c == 0x1.9999ap-4f);
	const WheelSlipParameters p{1, 1, 0.3f, 0.5f, 0.1f, 0.1f, 0.1f, 0.2f};
	WheelSlipInput slip{2, 0, 1, 0.01f, 0, 0};
	auto value = updateWheelSlipForContact(slip, p, WheelSlipContact::Loaded);
	CHECK(value && std::fabs(value->longitudinal - 0.044444444f) < 1e-6f &&
	      std::fabs(value->angle - 0.01f) < 1e-6f);
	slip.wheelSpeed = 1000; slip.lateralSpeed = -1000;
	value = updateWheelSlip(slip, p);
	CHECK(value && value->longitudinal == 1 && value->angle == -1);
	slip.wheelSpeed = 2; slip.lateralSpeed = 1;
	slip.roadSpeed = 5.1f;
	slip.wheelSpeed = 7.1f;
	value = updateWheelSlip(slip, p);
	CHECK(value && std::fabs(value->longitudinal - ((2.0f / 5.1f + 0.044444444f) * 0.5f)) < 1e-6f &&
	      std::fabs(value->angle - ((std::atan(1.0f / 5.1f) + 0.01f) * 0.5f)) < 1e-6f);
	slip.roadSpeed = -20;
	slip.wheelSpeed = -18;
	value = updateWheelSlip(slip, p);
	CHECK(value && std::fabs(value->longitudinal - 0.1f) < 1e-6f &&
	      std::fabs(value->angle - std::atan(0.05f)) < 1e-6f);
	slip.previousLongitudinal = 0.3f; slip.previousAngle = -0.4f;
	value = updateWheelSlipForContact(slip, p, WheelSlipContact::NoLoad);
	CHECK(value && value->longitudinal == 0.3f && value->angle == -0.4f);
	for (auto contact : {WheelSlipContact::NoSuspension, WheelSlipContact::Separated, WheelSlipContact::NoSurface}) {
		value = updateWheelSlipForContact(slip, p, contact);
		CHECK(value && value->longitudinal == 0 && value->angle == 0);
	}
	auto invalid = p;
	invalid.longitudinalRelaxation = 0;
	CHECK(!updateWheelSlip(slip, invalid));
	slip.wheelSpeed = std::numeric_limits<float>::quiet_NaN();
	CHECK(!updateWheelSlip(slip, p));
	std::array<float, 11> curve{1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0};
	auto force = evaluateWheelCurve(curve, 1, 2, WheelCurveMode::Signed);
	CHECK(force && std::fabs(*force - std::sqrt(2.0f)) < 1e-6f);
	force = evaluateWheelCurve(curve, -1, 2, WheelCurveMode::Signed);
	CHECK(force && std::fabs(*force + std::sqrt(2.0f)) < 1e-6f);
	force = evaluateWheelCurve(curve, -1, 2, WheelCurveMode::Nonnegative);
	CHECK(force && *force == 0);
	force = evaluateWheelCurve(curve, 1, 0, WheelCurveMode::Signed);
	CHECK(force && *force == 0);
	curve[1] = 0.1f;
	force = evaluateWheelCurve(curve, 1, 2, WheelCurveMode::Signed);
	CHECK(force && std::fabs(*force - (2.4f / std::sqrt(2.44f))) < 1e-6f);
	curve[0] = 0;
	force = evaluateWheelCurve(curve, 1, 2, WheelCurveMode::Signed);
	CHECK(force && *force == 0);
	force = evaluateWheelCurve(kStockSignedWheelCurve, 1, 100, WheelCurveMode::Signed);
	CHECK(force && *force < 0);
	force = evaluateWheelCurve(kStockSignedWheelCurve, 1, 100, WheelCurveMode::Nonnegative);
	CHECK(force && *force == 0);
	CHECK(!evaluateWheelCurve(curve, 1, -1, WheelCurveMode::Signed));
	curve[0] = std::numeric_limits<float>::quiet_NaN();
	CHECK(!evaluateWheelCurve(curve, 1, 2, WheelCurveMode::Signed));
	WheelContactForceInput contact{{0,1,0}, 3, 4, 100, 2, 0.2f, 0.3f};
	auto localForce = wheelContactLocalForce(contact);
	CHECK(localForce && std::fabs((*localForce)[0] - 3) < 1e-6f &&
	      std::fabs((*localForce)[1]) < 1e-6f && std::fabs((*localForce)[2] + 3) < 1e-6f);
	contact.localNormal = {1,0,0};
	localForce = wheelContactLocalForce(contact);
	CHECK(localForce && std::fabs((*localForce)[0]) < 1e-6f &&
	      std::fabs((*localForce)[1] + 3) < 1e-6f && std::fabs((*localForce)[2] + 3) < 1e-6f);
	contact.localNormal = {0,0,1};
	localForce = wheelContactLocalForce(contact);
	CHECK(localForce && (*localForce)[0] == 0 && (*localForce)[1] == 0 && (*localForce)[2] == 0);
	contact.surfaceRolling = std::numeric_limits<float>::quiet_NaN();
	CHECK(!wheelContactLocalForce(contact));
	TireSlipForceInput tire{};
	tire.curve = {1,0,1,0,1,0,0,0,0,0,0};
	tire.slip = {0,1,0,0.01f,0,0};
	tire.slipParameters = {1,1,0.3f,0.5f,1,0,0,0};
	tire.effectiveLoad = 2; tire.surfaceFriction = 1; tire.wheelFriction = 1;
	tire.longitudinalScale = 1; tire.radius = 1; tire.inverseWheelInertia = 1;
	tire.angularSpeed = -2; tire.angularGain = 2.8f;
	auto tireForce = calculateTireSlipForce(tire);
	CHECK(tireForce && std::fabs(tireForce->slip.longitudinal-1) < 1e-6f &&
	      std::fabs(tireForce->angularDelta-1) < 1e-5f &&
	      std::fabs(tireForce->torqueDelta-1) < 1e-5f);
	CHECK(tireForce && tireForce->longitudinalForce > 0 && tireForce->longitudinalForce < 1);
	tire.angularGain = 0;
	tireForce = calculateTireSlipForce(tire);
	CHECK(tireForce && std::fabs(tireForce->longitudinalForce-std::sqrt(2.0f)) < 1e-5f &&
	      tireForce->angularDelta == 0);
	tire.stiffnessNumerator = 0; tire.stiffnessDenominator = 2;
	tireForce = calculateTireSlipForce(tire);
	CHECK(tireForce && tireForce->longitudinalForce > 0 && tireForce->longitudinalForce < 1);
	tire.angularSpeed = -1;
	tireForce = calculateTireSlipForce(tire);
	CHECK(tireForce && tireForce->magnitude == 0 && tireForce->longitudinalForce == 0);
	tire.surfaceFriction = std::numeric_limits<float>::quiet_NaN();
	CHECK(!calculateTireSlipForce(tire));
	return failures ? 1 : 0;
}
