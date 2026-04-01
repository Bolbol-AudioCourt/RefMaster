# Bolbol RefMaster — Audio Behavior Specification

## 1. Overview

Bolbol RefMaster is a reference-based mastering plugin that analyzes the tonal balance of a reference track and applies a controlled, musical EQ correction to the input signal.

The goal is not to perfectly replicate the reference, but to **reduce tonal differences in a way that preserves musicality and avoids over-processing**.

---

## 2. Inputs and Outputs

### Input

- Stereo audio stream (host DAW master channel)
    
- Optional reference audio file (stereo)
    

### Output

- Stereo audio stream with applied EQ correction
    

---

## 3. Operational States

### 3.1 No Reference Loaded

- Plugin passes audio through unchanged
    
- Real-time spectrum of input is displayed
    
- No processing is applied
    

---

### 3.2 Reference Loaded

- Reference audio is analyzed offline
    
- An averaged frequency spectrum is computed and stored
    
- Processing becomes available
    

---

## 4. Core Processing Behavior

### 4.1 Spectral Analysis

Both input and reference signals are analyzed using FFT.

- Frequency domain representation is computed
    
- Magnitude spectrum is averaged over time
    
- Short-term fluctuations are smoothed to ensure stability
    

---

### 4.2 Spectral Difference

A difference curve is computed:

> Difference = Reference Spectrum − Input Spectrum

This represents the tonal adjustments required to move the input toward the reference.

---

### 4.3 Matching Philosophy

The system does NOT attempt to perfectly match the reference.

Instead, it follows this principle:

> “Apply the minimum necessary correction to reduce tonal imbalance while preserving the character of the original signal.”

---

## 5. EQ Generation

The raw difference curve is transformed into a set of parametric EQ bands.

### 5.1 Band Detection

- Significant peaks and dips are identified
    
- Minor fluctuations are ignored
    
- Emphasis is placed on perceptually important regions
    

---

### 5.2 Band Parameters

Each EQ band consists of:

- Frequency (Hz)
    
- Gain (dB)
    
- Q (bandwidth)
    

---

### 5.3 Band Constraints

To ensure musical results:

- Maximum gain: ±6 dB
    
- Minimum Q: avoids extremely narrow spikes
    
- Maximum number of bands: limited (e.g., 6–12)
    

---

## 6. Processing Constraints (Safety Layer)

### 6.1 Low Frequency Protection

- Frequencies below ~40 Hz are not aggressively boosted
    
- Prevents sub buildup and instability
    

---

### 6.2 Harshness Control

- Upper-mid range (~2kHz–6kHz) corrections are softened
    
- Prevents harsh or fatiguing results
    

---

### 6.3 Smoothing

- EQ curve is smoothed to avoid unnatural resonances
    
- Rapid spectral variations are ignored
    

---

### 6.4 Overcorrection Prevention

- If input and reference are already similar, minimal changes are applied
    
- Small differences are deprioritized
    

---

## 7. User Control Mapping

### 7.1 Match Amount

Controls overall intensity of EQ correction:

- 0% → no processing
    
- 100% → full computed correction (within constraints)
    

---

### 7.2 Smoothing Control

- Reduces number of EQ bands
    
- Produces broader, more musical adjustments
    

---

### 7.3 Reference Mix (optional)

- Allows blending between dry and processed signal
    

---

## 8. Time Behavior

### 8.1 Reference Analysis

- Performed offline
    
- Stable and averaged over entire file or selection
    

---

### 8.2 Input Analysis

- Real-time FFT
    
- Smoothed over time to prevent jitter in EQ updates
    

---

## 9. Edge Case Handling

### 9.1 Loudness Mismatch

- Spectral comparison is independent of absolute loudness
    
- Matching is based on relative frequency balance
    

---

### 9.2 Sparse or Unbalanced Reference

- Plugin avoids extreme corrections if reference has missing frequency content
    
- Safeguards prevent boosting noise or silence
    

---

### 9.3 Already Matched Signals

- Minimal correction is applied
    
- System favors transparency over unnecessary processing
    

---

## 10. Design Principles

- **Transparency over aggression**
    
- **Musicality over mathematical accuracy**
    
- **User control over full automation**
    
- **Stability over reactivity**
    

---

## 11. Summary

Bolbol RefMaster acts as an intelligent EQ assistant that:

- Listens to both signals
    
- Identifies meaningful tonal differences
    
- Applies controlled, explainable corrections
    

It is designed to behave like a cautious mastering engineer, not an automatic processor.