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
#include <boost/foreach.hpp>
#include <algorithm>

using namespace gnuradio;

void ElementImpl::mark_done(const tsbe::TaskInterface &task_iface)
{
    if (this->done) return; //can re-enter checking done first

    //mark down the new state
    this->active = false;
    this->done = true;

    //release upstream, downstream, and executor tokens
    this->token_pool.clear();

    //release allocator tokens, buffers can now call deleters
    this->output_buffer_tokens.clear();

    //release all buffers in queues
    this->input_queues.clear();
    this->output_queues.clear();

    //tell the upstream and downstram to re-check their tokens
    //this is how the other blocks know who is interested,
    //and can decide based on interest to set done or not
    for (size_t i = 0; i < task_iface.get_num_inputs(); i++)
    {
        task_iface.post_upstream(i, CheckTokensMessage());
    }
    for (size_t i = 0; i < task_iface.get_num_outputs(); i++)
    {
        task_iface.post_downstream(i, CheckTokensMessage());
    }

    std::cout << "This one: " << name << " is done..." << std::endl;
}

void ElementImpl::handle_task(const tsbe::TaskInterface &task_iface)
{
    //------------------------------------------------------------------
    //-- Decide if its possible to continue any processing:
    //-- Handle task may get called for incoming buffers,
    //-- however, not all ports may have available buffers.
    //------------------------------------------------------------------
    const bool all_inputs_ready = (~this->inputs_ready).none();
    const bool all_outputs_ready = (~this->outputs_ready).none();
    if (not (this->active and all_inputs_ready and all_outputs_ready)) return;

    const size_t num_inputs = task_iface.get_num_inputs();
    const size_t num_outputs = task_iface.get_num_outputs();
    const bool is_source = (num_inputs == 0);

    //------------------------------------------------------------------
    //-- sort the input tags before working
    //------------------------------------------------------------------
    for (size_t i = 0; i < num_inputs; i++)
    {
        if (not this->input_tags_changed[i]) continue;
        std::vector<Tag> &tags_i = this->input_tags[i];
        std::sort(tags_i.begin(), tags_i.end(), Tag::offset_compare);
        this->input_tags_changed[i] = false;
    }

    //------------------------------------------------------------------
    //-- Processing time!
    //------------------------------------------------------------------

    //std::cout << "calling work on " << name << std::endl;

    //reset work trackers for production/consumption
    size_t input_tokens_count = 0;
    for (size_t i = 0; i < num_inputs; i++)
    {
        input_tokens_count += this->input_tokens[i].use_count();
        //this->consume_items[i] = 0;

        ASSERT(this->input_history_items[i] == 0);

        const tsbe::Buffer &buff = this->input_queues[i].front();
        char *mem = ((char *)buff.get_memory()) + this->input_buff_offsets[i];
        const size_t bytes = buff.get_length() - this->input_buff_offsets[i];
        const size_t items = bytes/this->input_items_sizes[i];

        this->input_items[i]._mem = mem;
        this->input_items[i]._len = items;
        this->work_input_items[i] = mem;
        this->work_ninput_items[i] = items;
    }

    size_t num_output_items = ~0; //so big that it must std::min
    size_t output_tokens_count = 0;
    for (size_t i = 0; i < num_outputs; i++)
    {
        output_tokens_count += this->output_tokens[i].use_count();
        //this->produce_items[i] = 0;

        const tsbe::Buffer &buff = this->output_queues[i].front();
        char *mem = ((char *)buff.get_memory());
        const size_t bytes = buff.get_length();
        const size_t items = bytes/this->output_items_sizes[i];

        this->output_items[i]._mem = mem;
        this->output_items[i]._len = items;
        this->work_output_items[i] = mem;
        num_output_items = std::min(num_output_items, items);
    }

    //someone upstream or downstream holds no tokens, we are done!
    if (
        (num_inputs != 0 and input_tokens_count == num_inputs) or
        (num_outputs != 0 and output_tokens_count == num_outputs)
    ){
        this->mark_done(task_iface);
        return;
    }

    //start with source, this should be EZ
    int ret = 0;
    ret = block_ptr->Work(this->input_items, this->output_items);
    //VAR(ret);
    if (ret == Block::WORK_DONE)
    {
        this->mark_done(task_iface);
        return;
    }
    const size_t noutput_items = size_t(ret);

    //now to deal with consumption and production
    for (size_t i = 0; i < num_inputs; i++)
    {
        ASSERT(enable_fixed_rate or ret != Block::WORK_CALLED_PRODUCE);
        const size_t items = (enable_fixed_rate)? (myulround((noutput_items/this->relative_rate))) : this->consume_items[i];
        this->consume_items[i] = 0;

        this->items_consumed[i] += items;
        const size_t bytes = items*this->input_items_sizes[i];
        this->input_buff_offsets[i] += bytes;
        tsbe::Buffer &buff = this->input_queues[i].front();
        if (buff.get_length() >= this->input_buff_offsets[i])
        {
            this->input_queues[i].pop();
            this->inputs_ready.set(i, not this->input_queues[i].empty());
            this->input_buff_offsets[i] = 0;
        }
    }
    for (size_t i = 0; i < num_outputs; i++)
    {
        const size_t items = (ret == Block::WORK_CALLED_PRODUCE)? this->produce_items[i] : noutput_items;
        this->produce_items[i] = 0;

        this->items_produced[i] += items;
        const size_t bytes = items*this->output_items_sizes[i];
        tsbe::Buffer &buff = this->output_queues[i].front();
        buff.get_length() = bytes;
        task_iface.post_downstream(i, buff);
        this->output_queues[i].pop();
        this->outputs_ready.set(i, not this->output_queues[i].empty());
    }

    //0) figure out what we have for input data
    //1) calculate the possible num output items
    //2) take into account the item multiple
    //3) allocate some buffers
    //4) work....

    //block_ptr->forecast(100


    //TODO set deactive when work returns DONE

    //TODO source blocks should call work again until exhausted output buffers

    //------------------------------------------------------------------
    //-- trim the input tags that are past the consumption zone
    //-- and post trimmed tags to the downstream based on policy
    //------------------------------------------------------------------
    for (size_t i = 0; i < num_inputs; i++)
    {
        std::vector<Tag> &tags_i = this->input_tags[i];
        const size_t items_consumed_i = this->items_consumed[i];
        size_t last = 0;
        while (last < tags_i.size() and tags_i[last].offset < items_consumed_i)
        {
            last++;
        }

        //follow the tag propagation policy before erasure
        switch (this->tag_prop_policy)
        {
        case Block::TPP_DONT: break; //well that was ez
        case Block::TPP_ALL_TO_ALL:
            for (size_t out_i = 0; out_i < num_outputs; out_i++)
            {
                for (size_t tag_i = 0; tag_i < last; tag_i++)
                {
                    Tag t = tags_i[tag_i];
                    t.offset = myullround(t.offset * this->relative_rate);
                    task_iface.post_downstream(out_i, t);
                }
            }
            break;
        case Block::TPP_ONE_TO_ONE:
            if (i < num_outputs)
            {
                for (size_t tag_i = 0; tag_i < last; tag_i++)
                {
                    Tag t = tags_i[tag_i];
                    t.offset = myullround(t.offset * this->relative_rate);
                    task_iface.post_downstream(i, t);
                }
            }
            break;
        };

        //now its safe to perform the erasure
        if (last != 0) tags_i.erase(tags_i.begin(), tags_i.begin()+last);
    }

    //------------------------------------------------------------------
    //-- now commit all tags in the output queue to the downstream
    //------------------------------------------------------------------
    for (size_t i = 0; i < num_outputs; i++)
    {
        BOOST_FOREACH(const Tag &t, this->output_tags[i])
        {
            task_iface.post_downstream(i, t);
        }
        this->output_tags[i].clear();
    }
}
