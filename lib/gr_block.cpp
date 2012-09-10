//
// Copyright 2012 Josh Blum
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with io_sig program.  If not, see <http://www.gnu.org/licenses/>.

#include "element_impl.hpp"
#include <gr_block.h>

gr_block::gr_block(void)
{
    //NOP
}

gr_block::gr_block(
    const std::string &name,
    gr_io_signature_sptr input_signature,
    gr_io_signature_sptr output_signature
):
    gnuradio::Block(name)
{
    this->set_fixed_rate(false);
    this->set_input_signature(input_signature);
    this->set_output_signature(output_signature);
}

int gr_block::Work(
    const InputItems &input_items,
    const OutputItems &output_items
){
    return this->general_work(
        (*this)->work_noutput_items,
        (*this)->work_ninput_items,
        (*this)->work_input_items,
        (*this)->work_output_items
    );
}

int gr_block::general_work(
    int noutput_items,
    gr_vector_int &ninput_items,
    gr_vector_const_void_star &input_items,
    gr_vector_void_star &output_items
){
    throw std::runtime_error("gr_block subclasses must overload general_work!");
}

void gr_block::set_alignment(const size_t)
{
    //TODO
    //probably dont need this since buffers always start aligned
    //and therefore alignment is always re-acheived
}

bool gr_block::is_unaligned(void)
{
    //TODO
    //probably dont need this since volk dispatcher checks alignment
    //32 byte aligned is good enough for you
    return ((*this)->work_io_ptr_mask & ptrdiff_t(GRAS_MAX_ALIGNMENT-1)) != 0;
}

size_t gr_block::fixed_rate_noutput_to_ninput(const size_t noutput_items)
{
    return (*this)->input_history_items[0] +
            myulround((noutput_items/(*this)->relative_rate));
}

size_t gr_block::interpolation(void) const
{
    return size_t(1.0*this->relative_rate());
}

void gr_block::set_interpolation(const size_t interp)
{
    this->set_relative_rate(1.0*interp);
}

size_t gr_block::decimation(void) const
{
    return size_t(1.0/this->relative_rate());
}

void gr_block::set_decimation(const size_t decim)
{
    this->set_relative_rate(1.0/decim);
}
