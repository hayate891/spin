/*********************************************************************
Matt Marchant 2016
http://trederia.blogspot.com

SpIn - Zlib license.

This software is provided 'as-is', without any express or
implied warranty. In no event will the authors be held
liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute
it freely, subject to the following restrictions:
1. The origin of this software must not be misrepresented;
you must not claim that you wrote the original software.
If you use this software in a product, an acknowledgment
in the product documentation would be appreciated but
is not required.
2. Altered source versions must be plainly marked as such,
and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any
source distribution.
*********************************************************************/

#include <I8080/I8080.hpp>

#include <cstring>
#include <cassert>

//this hides some of the horrors of using pointer to member functions
#define EXEC_OPCODE(opcode) ((*this).*(m_opcodes[opcode]))()

using namespace I8080;

namespace
{
    //maps number of I8080 cycles take by each opcode
    const std::array<Byte, 256> opCycles = 
    {
        4,  10, 7,  5,  5,  5,  7,  4,  0,  10, 7,  5,  5,  5,  7,  4,
        0,  10, 7,  5,  5,  5,  7,  4,  0,  10, 7,  5,  5,  5,  7,  4,
        0,  10, 16, 5,  5,  5,  7,  4,  0,  10, 7,  5,  5,  5,  7,  11,
        0,  10, 13, 0,  5,  5,  7,  0,  0,  10, 13, 5,  5,  5,  7,  0,
        5,  5,  5,  5,  5,  5,  5,  4,  5,  5,  5,  5,  5,  5,  5,  11,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  7,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
        5,  10, 10, 10, 11, 11, 0,  11, 11, 10, 0,  10, 11, 17, 7,  11,
        5,  10, 10, 10, 11, 11, 7,  11, 11, 0,  10, 10, 11, 0,  7,  11,
        11, 10, 10, 18, 11, 11, 7,  11, 11, 5,  10, 4,  11, 0,  7,  11,
        11, 10, 10, 4,  11, 11, 7,  11, 11, 5,  10, 4,  11, 0,  7,  11
    };
}

CPU::Registers::Registers()
    : A(m_a), B(m_bc.b.h), C(m_bc.b.l), D(m_de.b.h), E(m_de.b.l), H(m_hl.b.h), L(m_hl.b.l),
    M(m_hl.w), BC(m_bc.w), DE(m_de.w), HL(m_hl.w), programCounter(m_programCounter.w), stackPointer(m_stackPointer.w),
    m_a(0)
{
    m_bc.w = 0;
    m_de.w = 0;
    m_hl.w = 0;
    m_programCounter.w = 0;
    m_stackPointer.w = 0xFFFF; //stack is at end of RAM and moves downwards
}

CPU::CPU()
    : m_cycleCount      (0),
    m_currentOpcode     (0),
    m_shiftByte0        (0),
    m_shiftByte1        (0),
    m_shiftOffset       (0),
    m_stopped           (false),
    m_interruptEnabled  (false),
    m_interruptPending  (0)
{
    m_registers.BC = 0;
    m_registers.DE = 0;
    m_registers.HL = 0;
    m_registers.programCounter = 0;
    m_registers.stackPointer = 0xFFFF;

    m_flags.psw = 0;
    m_flags.s = 0;
    m_flags.z = 0;
    m_flags.ac = 0;
    m_flags.p = 0;
    m_flags.cy = 0;

    std::memset(m_memory.data(), 0, MEM_SIZE);
    m_memory[1] = 0xC3; //jumps to zero in inf loop by default
    std::memset(m_ports.data(), 0, sizeof(Word) * PORT_COUNT);

    //opcode pointer table - EEEEE these should all be static :S
    m_opcodes =
    {
        &CPU::nop,     &CPU::lxib,    &CPU::staxb,   &CPU::inxb,    &CPU::inrb,    &CPU::dcrb,    &CPU::mvib,    &CPU::rlc,     &CPU::notImpl, &CPU::dadb,    &CPU::ldaxb,   &CPU::dcxb,    &CPU::inrc,    &CPU::dcrc,    &CPU::mvic,    &CPU::rrc,
        &CPU::notImpl, &CPU::lxid,    &CPU::staxd,   &CPU::inxd,    &CPU::inrd,    &CPU::dcrd,    &CPU::mvid,    &CPU::ral,     &CPU::notImpl, &CPU::dadd,    &CPU::ldaxd,   &CPU::dcxd,    &CPU::inre,    &CPU::dcre,    &CPU::mvie,    &CPU::rar,
        &CPU::notImpl, &CPU::lxih,    &CPU::shld,    &CPU::inxh,    &CPU::inrh,    &CPU::dcrh,    &CPU::mvih,    &CPU::daa,     &CPU::notImpl, &CPU::dadh,    &CPU::lhld,    &CPU::dcxh,    &CPU::inrl,    &CPU::dcrl,    &CPU::mvil,    &CPU::cma,
        &CPU::notImpl, &CPU::lxisp,   &CPU::sta,     &CPU::notImpl, &CPU::inrm,    &CPU::dcrm,    &CPU::mvim,    &CPU::stc,     &CPU::notImpl, &CPU::dadsp,   &CPU::lda,     &CPU::dcxsp,   &CPU::inra,    &CPU::dcra,    &CPU::mvia,    &CPU::cmc,
        &CPU::movbb,   &CPU::movbc,   &CPU::movbd,   &CPU::movbe,   &CPU::movbh,   &CPU::movbl,   &CPU::movbm,   &CPU::movba,   &CPU::movcb,   &CPU::movcc,   &CPU::movcd,   &CPU::movce,   &CPU::movch,   &CPU::movcl,   &CPU::movcm,   &CPU::movca,
        &CPU::movdb,   &CPU::movdc,   &CPU::movdd,   &CPU::movde,   &CPU::movdh,   &CPU::movdl,   &CPU::movdm,   &CPU::movda,   &CPU::moveb,   &CPU::movec,   &CPU::moved,   &CPU::movee,   &CPU::moveh,   &CPU::movel,   &CPU::movem,   &CPU::movea,
        &CPU::movhb,   &CPU::movhc,   &CPU::movhd,   &CPU::movhe,   &CPU::movhh,   &CPU::movhl,   &CPU::movhm,   &CPU::movha,   &CPU::movlb,   &CPU::movlc,   &CPU::movld,   &CPU::movle,   &CPU::movlh,   &CPU::movll,   &CPU::movlm,   &CPU::movla,
        &CPU::movmb,   &CPU::movmc,   &CPU::movmd,   &CPU::movme,   &CPU::movmh,   &CPU::movml,   &CPU::hlt,     &CPU::movma,   &CPU::movab,   &CPU::movac,   &CPU::movad,   &CPU::movae,   &CPU::movah,   &CPU::moval,   &CPU::movam,   &CPU::movaa,
        &CPU::addb,    &CPU::addc,    &CPU::addd,    &CPU::adde,    &CPU::addh,    &CPU::addl,    &CPU::addm,    &CPU::adda,    &CPU::adcb,    &CPU::adcc,    &CPU::adcd,    &CPU::adce,    &CPU::adch,    &CPU::adcl,    &CPU::adcm,    &CPU::adca,
        &CPU::subb,    &CPU::subc,    &CPU::subd,    &CPU::sube,    &CPU::subh,    &CPU::subl,    &CPU::subm,    &CPU::sbba,    &CPU::sbbb,    &CPU::sbbc,    &CPU::sbbd,    &CPU::sbbe,    &CPU::sbbh,    &CPU::sbbl,    &CPU::sbbm,    &CPU::sbba,
        &CPU::anab,    &CPU::anac,    &CPU::anad,    &CPU::anae,    &CPU::anah,    &CPU::anal,    &CPU::anam,    &CPU::anaa,    &CPU::xrab,    &CPU::xrac,    &CPU::xrad,    &CPU::xrae,    &CPU::xrah,    &CPU::xral,    &CPU::xram,    &CPU::xraa,
        &CPU::orab,    &CPU::orac,    &CPU::orad,    &CPU::orae,    &CPU::orah,    &CPU::oral,    &CPU::oram,    &CPU::oraa,    &CPU::cmpb,    &CPU::cmpc,    &CPU::cmpd,    &CPU::cmpe,    &CPU::cmph,    &CPU::cmpl,    &CPU::cmpm,    &CPU::cmpa,
        &CPU::rnz,     &CPU::popb,    &CPU::jnz,     &CPU::jmp,     &CPU::cnz,     &CPU::pushb,   &CPU::adi,     &CPU::rst0,    &CPU::rz,      &CPU::ret,     &CPU::jz,      &CPU::notImpl, &CPU::cz,      &CPU::call,    &CPU::aci,     &CPU::rst1,
        &CPU::rnc,     &CPU::popd,    &CPU::jnc,     &CPU::out,     &CPU::cnc,     &CPU::pushd,   &CPU::sui,     &CPU::rst2,    &CPU::rc,      &CPU::notImpl, &CPU::jc,      &CPU::in,      &CPU::cc,      &CPU::notImpl, &CPU::sbi,     &CPU::rst3,
        &CPU::rpo,     &CPU::poph,    &CPU::jpo,     &CPU::xthl,    &CPU::cpo,     &CPU::pushh,   &CPU::ani,     &CPU::rst4,    &CPU::cpe,     &CPU::pchl,    &CPU::jpe,     &CPU::xchg,    &CPU::cpe,     &CPU::notImpl, &CPU::xri,     &CPU::rst5,
        &CPU::cp,      &CPU::poppsw,  &CPU::jp,      &CPU::di,      &CPU::cp,      &CPU::pushpsw, &CPU::ori,     &CPU::rst6,    &CPU::rm,      &CPU::sphl,    &CPU::jm,      &CPU::ei,      &CPU::cm,      &CPU::notImpl, &CPU::cpi,     &CPU::rst7
    };
}

//public
void CPU::reset()
{   
    m_cycleCount = 0;
    m_currentOpcode = 0;
    m_shiftByte0 = 0;
    m_shiftByte1 = 0;
    m_shiftOffset = 0;
    m_stopped = false;
    m_interruptEnabled = false;
    m_interruptPending = 0;

    m_registers.A = 0;
    m_registers.BC = 0;
    m_registers.DE = 0;
    m_registers.HL = 0;
    m_registers.programCounter = 0;
    m_registers.stackPointer = 0xFFFF;

    m_flags.psw = 0;
    m_flags.s = 0;
    m_flags.z = 0;
    m_flags.ac = 0;
    m_flags.p = 0;
    m_flags.cy = 0;

    std::memset(m_memory.data(), 0, MEM_SIZE);
    m_memory[1] = 0xC3; //jumps to zero in inf loop by default
    std::memset(m_ports.data(), 0, sizeof(Word) * PORT_COUNT);
}

void CPU::update(std::int32_t count)
{
    assert(count > 0);

    //fetch the opcode from memory
    //then execute it and update the number of CPU
    //cycles taken for that opcode
    m_cycleCount = count;
    while (m_cycleCount > 0)
    {
        m_currentOpcode = m_memory[m_registers.programCounter];
        EXEC_OPCODE(m_currentOpcode);
        m_cycleCount -= opCycles[m_currentOpcode];
    }
}

void CPU::raiseInterrupt(Byte id)
{
    static const int ISR_Size = 8;
    static const int ISR_Cycles = 11;

    if (m_interruptEnabled)
    {
        m_interruptEnabled = false;
        m_interruptPending = 0;
        //push the current working position on to the stack
        pushWord(m_registers.programCounter);
        //jump the program counter to the ISR address
        m_registers.programCounter = id * ISR_Size;
        m_cycleCount -= ISR_Cycles;
    }
    else
    {
        m_interruptPending = 0x80 | id;
    }
}

bool CPU::loadROM(const std::string& path, Word address)
{
    return false;
}

std::string CPU::getInfo() const
{
    std::string str
    (
        "Program Counter: " + std::to_string(m_registers.programCounter) +
        "\nCurrent Opcode: " + std::to_string(m_currentOpcode) 
    );
    return std::move(str);
}

//private
void CPU::pushWord(Word word)
{
    m_registers.stackPointer -= 2;
    m_memory[m_registers.stackPointer] = word & 0x00FF;
    m_memory[m_registers.stackPointer + 1] = ((word >> 8) & 0xFF);
}

Word CPU::popWord()
{
    auto word = (m_memory[m_registers.stackPointer] << 8) | m_memory[m_registers.stackPointer];
    m_registers.stackPointer += 2;
    return word;
}

Word CPU::getWord(Word address)
{
    return ((m_memory[address + 1] << 8) | m_memory[address]);
}