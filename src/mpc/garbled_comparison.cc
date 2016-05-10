/*
 * Copyright 2013-2015 Raphael Bost
 *
 * This file is part of ciphermed.

 *  ciphermed is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  ciphermed is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with ciphermed.  If not, see <http://www.gnu.org/licenses/>. 2
 *
 */

#include "garbled_comparison.hh"

using namespace std;

#include<iostream>

#include <justGarble/gates.h>

int OneBitCompareCircuit(GarbledCircuit *garbledCircuit, GarblingContext *garblingContext, int* inputs, int* outputs) {

    // inputs[0] = x
    // inputs[1] = y
    // inputs[2] = c

    int x = inputs[0];
    int y = inputs[1];
    int c = inputs[2];

    int x_xor_c;
    int y_xor_c;
    int and_wire;
    int x_xor_and; // output

    x_xor_c = getNextWire(garblingContext);
    XORGate(garbledCircuit, garblingContext, x, c, x_xor_c);

    y_xor_c = getNextWire(garblingContext);
    XORGate(garbledCircuit, garblingContext, y, c, y_xor_c);

    and_wire  = getNextWire(garblingContext);
    ANDGate(garbledCircuit, garblingContext, x_xor_c, y_xor_c,and_wire);

    x_xor_and = getNextWire(garblingContext);
    XORGate(garbledCircuit, garblingContext, x, and_wire, x_xor_and);

    outputs[0] = x_xor_and;

    return 0;
}

int FirstRound_OneBitCompareCircuit(GarbledCircuit *garbledCircuit, GarblingContext *garblingContext, int* inputs, int* outputs) {
    // inputs[0] = x
    // inputs[1] = y
    // Same as the one before (OneBitCompareCircuit) but where c is forced to 0 (
    int x = inputs[0];
    int y = inputs[1];

    int x_and_y;
    int x_xor_and; // output


    x_and_y  = getNextWire(garblingContext);
    ANDGate(garbledCircuit, garblingContext, x, y,x_and_y);

    x_xor_and = getNextWire(garblingContext);
    XORGate(garbledCircuit, garblingContext, x, x_and_y, x_xor_and);

    outputs[0] = x_xor_and;

    return 0;
}


int CompareCircuit(GarbledCircuit *gc, GarblingContext *garblingContext, int n, int* inputs, int* outputs) {
    // waiting for 2*n+1 inputs where n the bit-length of integers to compare
    // inputs[2*n] is a bit c that we use to tune the circuit
    // x is encoded on even bits (inputs[0],...,inputs[2*n-2]
    // y is encoded on odd bits (inputs[1],...,inputs[2*n-1]


    FirstRound_OneBitCompareCircuit(gc, garblingContext, inputs, outputs);

    int in[3];

    int i = 0;
    for (i = 1; i < n; i++) {
        in[0] = inputs[2*i];
        in[1] = inputs[2*i+1];
        in[2] = outputs[0];

        OneBitCompareCircuit(gc, garblingContext, in, outputs);
    }

    return 0;
}


GarbledCircuit* create_comparison_circuit(GarblingContext *garblingContext, size_t l, OutputMap *om)
{
    GarbledCircuit * gc = (GarbledCircuit *)malloc(sizeof(GarbledCircuit));

    int n = 2*l+1; // # of input bits
    int m = 1; // one output bit : the result
    int q = 4*l-1; // number of gates : 2 (first round) + 4*(l-1) other rounds + 1 (final XOR)
    int r = n+4*l+1; // number of wires

    block *labels = (block*) malloc(sizeof(block) * 2 * n);
    block *outputbs = (block*) malloc(sizeof(block) * 2 * m);

    int *inp = (int *) malloc(sizeof(int) * n);
    countToN(inp, n);
    int outputs[1];

    OutputMap outputMap = outputbs;
    InputLabels inputLabels = labels;

    createInputLabels(labels, n);
    createEmptyGarbledCircuit(gc, n, m, q, r, inputLabels);

    startBuilding(gc, garblingContext);

    CompareCircuit(gc, garblingContext, l, inp, outputs);

    // now XOR the result with an extra bit in order to hide the result
    int out = getNextWire(garblingContext);
    XORGate(gc, garblingContext, inp[2*l], outputs[0], out);

    outputs[0] = out;

    finishBuilding(gc, garblingContext, outputMap, outputs);

    if (om) {
        *om = (OutputMap)outputMap;
    }

    gc->inputLabels = inputLabels;
    return gc;
}

GC_Compare_A::GC_Compare_A(const mpz_class &x, const size_t &l, GM &gm, gmp_randstate_t state)
: a_(x), bit_length_(l), gm_(gm)
{
    s_ = 1 - 2*gmp_urandomb_ui(state,1);
    gmp_randinit_set(randstate_, state);
}

void GC_Compare_A::prepare_circuit()
{
    GarblingContext garblingContext;

    gc_ = create_comparison_circuit(&garblingContext, bit_length_, NULL);
}

void GC_Compare_A::evaluateGC(InputLabels a_inputLabels, InputLabels b_inputLabels)
{
    int n = gc_->n;
    int m = gc_->m;
    block computedOutputMap[m];
    int outputVals[m];

    block extractedLabels[n];

    for (size_t i = 0; i < bit_length_; i++) {
        extractedLabels[2*i] = b_inputLabels[i];
        extractedLabels[2*i+1] = a_inputLabels[i];
    }
    extractedLabels[n-1] = b_inputLabels[bit_length_];


    evaluate(gc_, extractedLabels, &computedOutput_);
}

int GC_Compare_A::map_output(OutputMap outputMap)
{

    mapOutputs(outputMap, &computedOutput_, &blinded_res_, 1);

    return blinded_res_;
}

void GC_Compare_A::unblind(const mpz_class &enc_mask)
{
    if (blinded_res_ == 0) {
        res_ = enc_mask;
    }else{
        res_ = gm_.neg(enc_mask);
    }
}

vector<bool> GC_Compare_A::get_a_bits()
{
    vector<bool> bits(bit_length_);

    for (size_t i = 0; i < bit_length_; i++) {
        bits[i] = mpz_tstbit(a_.get_mpz_t(), i);
    }

    return bits;
}


GC_Compare_B::GC_Compare_B(const mpz_class &y, const size_t &l, GM_priv &gm, gmp_randstate_t state)
: b_(y), bit_length_(l), gm_(gm), mask_(0)
{
    mask_ = gmp_urandomb_ui(state,1);
}


void GC_Compare_B::prepare_circuit()
{
    GarblingContext garblingContext;

    gc_ = create_comparison_circuit(&garblingContext, bit_length_,&outputMap_);

    createInputLabels(gc_->inputLabels,gc_->n);

    garbleCircuit(gc_, gc_->inputLabels, outputMap_);
}

InputLabels GC_Compare_B::get_b_input_labels()
{
    block *b_ins = (block *)malloc((bit_length_+1)*sizeof(block));

//    int *inputs = (int *)malloc(2*bit_length_*sizeof(int));
//
//
//    for (size_t i = 0; i < bit_length_; i++) {
//        inputs[2*i+1] = 0;
//        inputs[2*i] = mpz_tstbit(b_.get_mpz_t(), i);
//    }

    int bit = 0;
    for (size_t i = 0; i < bit_length_; i++) {
        bit = mpz_tstbit(b_.get_mpz_t(), i);
        b_ins[i] = gc_->inputLabels[2*2*i + bit];
    }

    b_ins[bit_length_] = gc_->inputLabels[2*2*bit_length_ + mask_];
    return b_ins;
}

InputLabels GC_Compare_B::get_all_a_input_labels()
{
    block *a_ins = (block *)malloc(2*bit_length_*sizeof(block));

    for (size_t i = 0; i < bit_length_; i++) {
        a_ins[2*i] = gc_->inputLabels[2*(2*i+1)];
        a_ins[2*i+1] = gc_->inputLabels[2*(2*i+1) + 1];
    }


    return a_ins;
}

mpz_class GC_Compare_B::get_enc_mask()
{
    return gm_.encrypt(mask_);
}




void runProtocol(GC_Compare_A &party_a, GC_Compare_B &party_b, gmp_randstate_t state)
{
    party_a.prepare_circuit();
    party_b.prepare_circuit();

    GarbledCircuit *gc_a = party_a.get_garbled_circuit();
    GarbledCircuit *gc_b = party_b.get_garbled_circuit();


    size_t l = party_a.bit_length();
    int n = 2*l+1;
    int m = 1;

    int *inputs = (int *)malloc(n*sizeof(int));
    int *a_inputs = (int *)malloc(l*sizeof(int));

    char *bits_a = mpz_get_str(NULL,2,party_a.a_.get_mpz_t());

    mpz_class a = party_a.a_;
    mpz_class b = party_b.b_;


    party_a.set_global_key(party_b.get_global_key());
    GarbledTable *gt = party_b.get_garbled_table();
    party_a.set_garbled_table(gt);



    block *b_labels = party_b.get_b_input_labels();
    block *all_a_labels;
    block a_labels[l];

    // to get a_labels, you HAVE TO run some OT with B
    vector<bool> a_bits = party_a.get_a_bits();
    for (size_t i = 0; i < l; i++) {
        a_inputs[i] = a_bits[i];
    }
    all_a_labels = party_b.get_all_a_input_labels();
    extractLabels(a_labels, all_a_labels, a_inputs, l);

    int outputVals[m];


    party_a.evaluateGC(a_labels, b_labels);
    outputVals[0] = party_a.map_output(party_b.get_output_map());

    party_a.unblind(party_b.get_enc_mask());
}
