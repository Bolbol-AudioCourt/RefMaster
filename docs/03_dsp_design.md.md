# Bolbol RefMaster — DSP Design Specification

## 1. Overview

This document defines the signal processing architecture for Bolbol RefMaster.

The system is divided into four main stages:

1. Spectral Analysis (FFT)
    
2. Spectral Comparison
    
3. Curve Simplification
    
4. EQ Generation and Processing (IIR)
    

All processing is designed to be real-time safe and musically stable.

---

## 2. Spectral Analysis

### 2.1 FFT Configuration

- FFT Size: 2048 (initial)
    
- Overlap: 50%
    
- Window Function: Hann window
    

Rationale:

- 2048 provides a good balance between frequency resolution and latency
    
- Hann window reduces spectral leakage
    

---

### 2.2 Input Signal Analysis (Real-Time)

- Audio is buffered into a circular buffer
    
- FFT is performed when buffer is filled
    
- Magnitude spectrum is computed
    
- Spectrum is smoothed over time using exponential averaging
    

Smoothing formula:

- smoothed = α * current + (1 - α) * previous
    
- α ≈ 0.2 (tunable)
    

---

### 2.3 Reference Signal Analysis (Offline)

- Entire reference file is processed in chunks
    
- FFT applied to each chunk
    
- Magnitude spectra are averaged over time
    

Final output:

- Stable average spectrum of reference track
    

---

## 3. Spectral Comparison

### 3.1 Normalization

Before comparison:

- Convert magnitudes to decibels (log scale)
    
- Normalize both spectra to remove loudness bias
    

---

### 3.2 Difference Curve

Computed as:

Difference(f) = Reference(f) − Input(f)

Result:

- Positive values → boost required
    
- Negative values → cut required
    

---

### 3.3 Frequency Weighting (optional, later)

- Reduce sensitivity in very low (<40Hz) and very high (>16kHz) ranges
    
- Apply perceptual weighting if needed
    

---

## 4. Curve Smoothing

The raw difference curve is noisy and unsuitable for direct use.

### 4.1 Smoothing Method

- Apply moving average across frequency bins
    
- Window size: ~1/6 octave equivalent
    

Alternative (future):

- Gaussian smoothing
    

---

### 4.2 Result

- Broad, musically meaningful tonal shifts
    
- Removal of narrow spikes and artifacts
    

---

## 5. Peak Detection (Feature Extraction)

### 5.1 Goal

Reduce the smoothed curve into a small set of meaningful EQ bands.

---

### 5.2 Detection Strategy

- Identify local maxima (boost regions)
    
- Identify local minima (cut regions)
    
- Ignore insignificant variations (below threshold ~1 dB)
    

---

### 5.3 Band Selection

- Limit total bands to 6–12
    
- Prioritize:
    
    - Largest magnitude differences
        
    - Even distribution across spectrum
        

---

## 6. EQ Band Generation (IIR)

Each detected feature is converted into a parametric EQ band.

---

### 6.1 Band Parameters

For each band:

- Frequency:  
    → center of detected peak/dip
    
- Gain:  
    → scaled difference value (clamped to ±6 dB)
    
- Q (bandwidth):  
    → derived from width of peak  
    → wider peak = lower Q  
    → narrow peak = higher Q (within limits)
    

---

### 6.2 Constraints

- Gain limited to ±6 dB
    
- Q limited to safe range (e.g., 0.5 – 5.0)
    
- Bands are merged if too close
    

---

## 7. EQ Processing (Real-Time)

### 7.1 Filter Type

- Biquad IIR filters
    
- One filter per band
    

---

### 7.2 Processing Flow

Inside `processBlock()`:

For each sample:

- Pass signal through chain of EQ filters
    

Order:

- Low → high frequency bands
    

---

### 7.3 Stability Requirements

- No memory allocation in audio thread
    
- Filters pre-initialized
    
- Parameter updates handled safely
    

---

## 8. Parameter Mapping

### 8.1 Match Amount

- Scales gain of all EQ bands:
    

Final Gain = Computed Gain × Amount

---

### 8.2 Smoothing Control

- Affects:
    
    - curve smoothing width
        
    - number of detected bands
        

---

## 9. Performance Considerations

- FFT runs at reduced rate (not every sample)
    
- Use buffering to minimize CPU load
    
- Avoid heavy computations in audio thread
    

---

## 10. Future Extensions

(Not part of V1 but planned)

- LUFS analysis
    
- Stereo (Mid/Side) processing
    
- Dynamic EQ behavior
    
- FIR precision mode
    

---

## 11. Summary

The DSP system:

1. Analyzes both signals in frequency domain
    
2. Computes tonal differences
    
3. Simplifies into meaningful features
    
4. Reconstructs a musical EQ using IIR filters
    

This balances:

- Accuracy
    
- Performance
    
- Musical usability