/*
 * test_therm_math.c — Host-side CTest unit tests for the therm_math component.
 *
 * Build and run via:
 *   cd firmware/tests
 *   cmake -B build -DCMAKE_C_FLAGS="-Wall -Wextra" && cmake --build build
 *   ctest --test-dir build --output-on-failure
 */

#include "therm_math.h"
#include "board.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <float.h>

/* ── Simple assert framework ──────────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            g_pass++;                                                           \
            printf("  PASS: %s\n", msg);                                       \
        } else {                                                                \
            g_fail++;                                                           \
            printf("  FAIL: %s  [%s:%d]\n", msg, __FILE__, __LINE__);          \
        }                                                                       \
    } while (0)

#define CHECK_NEAR(a, b, tol, msg)                                              \
    CHECK(fabsf((float)(a) - (float)(b)) <= (float)(tol), msg)

/* ── Helper: invoke adc_to_resistance with BOARD constants ──────────────── */

static float board_adc_to_resistance(int16_t raw)
{
    return therm_math_adc_to_resistance(raw,
                                        BOARD_R_TOP_OHMS,
                                        BOARD_R_SERIES_OHMS,
                                        BOARD_ADS_VFS,
                                        BOARD_ADC_VREF,
                                        BOARD_ADC_MAX);
}

/* ── Test groups ─────────────────────────────────────────────────────────── */

/* therm_math_adc_to_resistance -------------------------------------------- */
static void test_adc_to_resistance(void)
{
    printf("\n[therm_math_adc_to_resistance]\n");

    /*
     * Mid-range test: raw_adc = 16000
     *   v     = (16000 / 32767) * 4.096 = 65536/32767 ≈ 2.00006 V
     *   denom = 3.3 - 2.00006            ≈ 1.29994
     *   r     = (2.00006 * 22100) / 1.29994 - 470 ≈ 33532 Ω
     *
     * We tolerate ±10 Ω to cover float rounding vs hand calculation.
     */
    {
        float r = board_adc_to_resistance(16000);
        CHECK_NEAR(r, 33532.0f, 10.0f,
                   "mid-range ADC=16000 → ~33532 Ω");
        CHECK(r > 0.0f && r < 1.0e9f,
              "mid-range result is a finite positive resistance");
    }

    /*
     * Low ADC: raw = 1000
     *   v ≈ (1000/32767)*4.096 ≈ 0.12500 V
     *   denom ≈ 3.3 - 0.125 = 3.175
     *   r ≈ (0.125 * 22100) / 3.175 - 470 ≈ 870.5 - 470 ≈ 400.5 Ω
     *
     * The result is physically plausible (thermistor hot), positive.
     */
    {
        float r = board_adc_to_resistance(1000);
        CHECK(r > 0.0f && r < 1.0e9f,
              "low ADC=1000 → finite positive resistance");
    }

    /*
     * ADC = 0: v = 0, denom = Vref = 3.3 (not near zero), but
     *   r = (0 * r_top) / 3.3 - r_series = -470 Ω → non-positive → sentinel
     */
    {
        float r = board_adc_to_resistance(0);
        CHECK(r == 1.0e9f,
              "ADC=0 → open-circuit sentinel 1e9");
    }

    /*
     * ADC = BOARD_ADC_MAX (32767): v = vfs = 4.096 V
     *   denom = 3.3 - 4.096 = -0.796 — magnitude > 1e-4, so no sentinel on
     *   denominator guard, but r = (4.096*22100)/(-0.796) - 470 ≈ negative → sentinel
     */
    {
        float r = board_adc_to_resistance((int16_t)BOARD_ADC_MAX);
        CHECK(r == 1.0e9f,
              "ADC=ADC_MAX → non-positive resistance clamped to sentinel 1e9");
    }

    /*
     * Near-zero denominator: find a raw value where v ≈ Vref = 3.3 V.
     *   raw_center = 3.3 * 32767 / 4.096 = 26399.2
     *
     * The guard fires when |vref - v| < 1e-4.
     * delta_raw = 1e-4 * 32767 / 4.096 ≈ 0.8 → only raw=26399 triggers it.
     *
     * raw=26399: v = 26399*4.096/32767 ≈ 3.29998 V, denom ≈ 0.000024 < 1e-4 → sentinel
     */
    {
        float r = board_adc_to_resistance(26399);
        CHECK(r == 1.0e9f,
              "near-zero denominator (v≈Vref) → sentinel 1e9");
    }

    /*
     * Monotonicity: resistance INCREASES with increasing ADC count.
     * Circuit: VCC → R_top → ADC_IN → R_series → R_therm → GND
     * Higher ADC → higher V_adc → colder thermistor → higher R_therm.
     * r = (V * R_top) / (Vref - V) - R_series grows as V grows.
     */
    {
        float r_low_adc  = board_adc_to_resistance(5000);
        float r_high_adc = board_adc_to_resistance(20000);
        bool both_valid = (r_low_adc < 1.0e9f) && (r_high_adc < 1.0e9f)
                       && (r_low_adc > 0.0f) && (r_high_adc > 0.0f);
        CHECK(both_valid && r_low_adc < r_high_adc,
              "resistance increases monotonically with increasing ADC count");
    }
}

/* therm_math_resistance_to_celsius ---------------------------------------- */
static void test_resistance_to_celsius(void)
{
    printf("\n[therm_math_resistance_to_celsius]\n");

    const sh_coeffs_t *def = &THERM_MATH_DEFAULT_COEFFS;

    /*
     * 100K NTC at nominal resistance (100 000 Ω) should be near 25°C.
     * Hand-calculated with default coeffs:
     *   ln(100000) = 11.51293
     *   ln³       = 1526.75
     *   1/T = 7.3223e-4 + 2.1322e-4*11.51293 + 1.1482e-7*1526.75
     *         ≈ 7.3223e-4 + 2.4541e-3 + 1.7530e-4
     *         ≈ 3.3614e-3
     *   T_K ≈ 297.5 K → T_C ≈ 24.4°C
     *
     * We accept ±2°C to accommodate float/double rounding.
     */
    {
        float t = therm_math_resistance_to_celsius(100000.0f, def);
        CHECK_NEAR(t, 24.4f, 2.0f,
                   "100kΩ (nominal) → ~24°C with default coeffs");
    }

    /*
     * General sanity: result must be in a physically plausible range
     * for a thermistor that will be used in meat/ambient environments.
     */
    {
        float t = therm_math_resistance_to_celsius(100000.0f, def);
        CHECK(t > -50.0f && t < 300.0f,
              "100kΩ → temperature in plausible range (-50..300°C)");
    }

    /*
     * Higher resistance (colder) → lower temperature.
     * Lower resistance (hotter) → higher temperature.
     */
    {
        float t_cold = therm_math_resistance_to_celsius(500000.0f, def);  // cold
        float t_hot  = therm_math_resistance_to_celsius(10000.0f,  def);  // hot
        CHECK(t_cold < t_hot,
              "higher resistance → lower temperature (NTC characteristic)");
    }

    /*
     * Extreme resistance: 1 Ω → very high temperature.
     * The function should not crash; result will be far above 0°C.
     */
    {
        float t = therm_math_resistance_to_celsius(1.0f, def);
        CHECK(!isnan(t) && !isinf(t),
              "resistance=1Ω → finite (non-crash) result");
    }

    /*
     * Resistance = 0 → log(0) = -Inf, which propagates through S-H and
     * may produce NaN or ±Inf. The function does not guard against this,
     * so we only check it does not crash (i.e. the process continues).
     * We flag the result rather than asserting a specific value.
     */
    {
        /* Call it — if it crashes the test binary dies and CTest reports failure. */
        float t = therm_math_resistance_to_celsius(0.0f, def);
        (void)t;
        CHECK(1, "resistance=0Ω → function returns without crashing");
    }

    /*
     * Resistance = +Inf → log(Inf) = +Inf, 1/Inf = 0 → T_K = Inf.
     * Again, just a no-crash check.
     */
    {
        float t = therm_math_resistance_to_celsius(INFINITY, def);
        (void)t;
        CHECK(1, "resistance=Inf → function returns without crashing");
    }
}

/* therm_math_ema_update ---------------------------------------------------- */
static void test_ema_update(void)
{
    printf("\n[therm_math_ema_update]\n");

    /*
     * NaN seed: first call must return new_value directly (no filtering).
     */
    {
        float result = therm_math_ema_update(NAN, 42.0f, 0.1f);
        CHECK_NEAR(result, 42.0f, 1e-5f,
                   "NaN seed → returns new_value directly");
    }

    /*
     * Alpha = 1.0: output equals input (identity, no smoothing).
     *   EMA = 1.0*new + 0.0*state = new
     */
    {
        float result = therm_math_ema_update(100.0f, 50.0f, 1.0f);
        CHECK_NEAR(result, 50.0f, 1e-4f,
                   "alpha=1.0 → output equals new_value (identity)");
    }

    /*
     * Alpha = 0.0: output equals state (frozen, ignores new input).
     *   EMA = 0.0*new + 1.0*state = state
     */
    {
        float result = therm_math_ema_update(100.0f, 50.0f, 0.0f);
        CHECK_NEAR(result, 100.0f, 1e-4f,
                   "alpha=0.0 → output equals state (frozen)");
    }

    /*
     * Convergence: applying the same value repeatedly must converge to it.
     * With alpha=0.1, after 200 iterations starting from 0, result should
     * be within 1e-3 of the target value 75.0.
     *
     * After N steps: EMA_N = target*(1-(1-alpha)^N)
     * (1-0.1)^200 = 0.9^200 ≈ 7.2e-10 → negligible error.
     */
    {
        float ema = 0.0f;
        for (int i = 0; i < 200; i++) {
            ema = therm_math_ema_update(ema, 75.0f, 0.1f);
        }
        CHECK_NEAR(ema, 75.0f, 1e-3f,
                   "EMA converges to target after 200 iterations (alpha=0.1)");
    }

    /*
     * Symmetry: EMA is a weighted average — result must lie strictly between
     * state and new_value for alpha in (0,1).
     */
    {
        float state = 0.0f;
        float new_val = 100.0f;
        float result = therm_math_ema_update(state, new_val, 0.3f);
        CHECK(result > state && result < new_val,
              "result lies strictly between state and new_value for alpha=0.3");
    }

    /*
     * Exact formula check: EMA = alpha*new + (1-alpha)*state
     *   state=10, new=20, alpha=0.25 → 0.25*20 + 0.75*10 = 5 + 7.5 = 12.5
     */
    {
        float result = therm_math_ema_update(10.0f, 20.0f, 0.25f);
        CHECK_NEAR(result, 12.5f, 1e-4f,
                   "EMA formula: state=10, new=20, alpha=0.25 → 12.5");
    }

    /*
     * Step response: alpha=0.5, starting from NaN, stepping to 8.0.
     * After 1 step: 8.0 (NaN seed)
     * After 2 steps: 0.5*8 + 0.5*8 = 8.0 (same value each step)
     */
    {
        float ema = therm_math_ema_update(NAN, 8.0f, 0.5f);
        ema = therm_math_ema_update(ema, 8.0f, 0.5f);
        CHECK_NEAR(ema, 8.0f, 1e-5f,
                   "constant input → EMA stays at that value");
    }
}

/* therm_math_sh_valid ------------------------------------------------------ */
static void test_sh_valid(void)
{
    printf("\n[therm_math_sh_valid]\n");

    /* Default coefficients are all positive → valid */
    CHECK(therm_math_sh_valid(&THERM_MATH_DEFAULT_COEFFS),
          "THERM_MATH_DEFAULT_COEFFS are valid");

    /* All positive → valid */
    {
        sh_coeffs_t c = {.a = 1e-4, .b = 1e-4, .c = 1e-7};
        CHECK(therm_math_sh_valid(&c),
              "all-positive coefficients → valid");
    }

    /* a = 0 → invalid */
    {
        sh_coeffs_t c = {.a = 0.0, .b = 1e-4, .c = 1e-7};
        CHECK(!therm_math_sh_valid(&c),
              "a=0 → invalid");
    }

    /* b = 0 → invalid */
    {
        sh_coeffs_t c = {.a = 1e-4, .b = 0.0, .c = 1e-7};
        CHECK(!therm_math_sh_valid(&c),
              "b=0 → invalid");
    }

    /* c = 0 → invalid */
    {
        sh_coeffs_t c = {.a = 1e-4, .b = 1e-4, .c = 0.0};
        CHECK(!therm_math_sh_valid(&c),
              "c=0 → invalid");
    }

    /* Negative a → invalid */
    {
        sh_coeffs_t c = {.a = -1e-4, .b = 1e-4, .c = 1e-7};
        CHECK(!therm_math_sh_valid(&c),
              "a<0 → invalid");
    }

    /* Negative b → invalid */
    {
        sh_coeffs_t c = {.a = 1e-4, .b = -1e-4, .c = 1e-7};
        CHECK(!therm_math_sh_valid(&c),
              "b<0 → invalid");
    }

    /* Negative c → invalid */
    {
        sh_coeffs_t c = {.a = 1e-4, .b = 1e-4, .c = -1e-7};
        CHECK(!therm_math_sh_valid(&c),
              "c<0 → invalid");
    }

    /* All zero → invalid */
    {
        sh_coeffs_t c = {.a = 0.0, .b = 0.0, .c = 0.0};
        CHECK(!therm_math_sh_valid(&c),
              "all-zero coefficients → invalid");
    }
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== therm_math unit tests ===\n");

    test_adc_to_resistance();
    test_resistance_to_celsius();
    test_ema_update();
    test_sh_valid();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);

    return (g_fail > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
