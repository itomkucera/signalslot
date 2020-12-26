#ifndef __ITOMSIGNAL_H__
#define __ITOMSIGNAL_H__

#include <functional>
#include <map>

/*************** IMPLEMENTATION DETAILS ***************/

namespace itom::detail
{
#define ERROR_ID (size_t)-1

/*
* Signal's interface used as a "forward declaration"
*/
class ISignalImpl
{
public:

    virtual void Disconnect(size_t slot_id) = 0;
};

template <typename... Args>
class SignalImpl final : public detail::ISignalImpl
{
    template <typename... Args>
    friend class Signal;

    using SlotContainer = std::map<size_t, std::function<void(Args...)>>;

public:

    SignalImpl() = default;

    // terminates the connection with the specific id
    void Disconnect(size_t slot_id) override
    {
        slots_.erase(slot_id);
    }

private:

    // slots must be stored here - wrapper could be destroyed by another thread
    // immediatelly after calling lock() inside the Connection -> would lead to crash
    SlotContainer slots_;

};

} // namespace itom::detail


/*************** PUBLIC API ***************/

// TODO: helpers for overloaded slot functors

#define EMIT //< makes emitting the signal more readable

namespace itom
{

/*
* Represents the signal-slot connection
*/
class Connection final
{
public:

    Connection(size_t slot_id, std::weak_ptr<detail::ISignalImpl> signal) :
        slot_id_{ slot_id },
        signal_{ signal }
    {

    }

    // terminates the connection, removes the slot from the signal
    void Disconnect()
    {
        if (auto shared_signal = signal_.lock())
        {
            shared_signal->Disconnect(slot_id_);
        }
    }

private:

    size_t slot_id_;

    std::weak_ptr<detail::ISignalImpl> signal_;
};

/*
* Extend this class to automatically terminate all the connections
* that depend on the instance of this class (this object was declared
* as a disconnector for the connection)
*/
class Disconnector
{
    template <typename... Args>
    friend class Signal;

    using ConnectionContainer = std::vector<Connection>;

public:

    Disconnector() = default;

    virtual ~Disconnector()
    {
        // terminate all the connections
        for (auto&& connection : connections_)
        {
            connection.Disconnect();
        }
    }

private:

    void AddConnection(Connection&& connection)
    {
        connections_.push_back(std::move(connection));
    }

    ConnectionContainer connections_;
};


/*
* Wrapper class for the signal
*/
template <typename... Args>
class Signal final
{
public:

    Signal() :
        impl_{ std::make_shared<SignalImpl>() }
    {

    }

    Signal(const Signal&) = delete;

    Signal(Signal&&) noexcept = delete;

    Signal& operator=(const Signal&) = delete;

    Signal& operator=(Signal&&) noexcept = delete;

    // creates a connection
    // slot - invocable with Args...
    template <typename S>
    Connection Connect(S&& slot)
    {
        // store the slot at the actual position
        impl_->slots_.emplace(actual_slot_id_, std::forward<S>(slot));

        return { actual_slot_id_++, impl_ };
    }

    // slot - non-static member function of disconnector, invocable with Args...
    // disconnector - inherits Disconnector
    template <typename S, typename D>
    Connection Connect(S&& slot, D* disconnector,
        typename std::enable_if_t<
            std::is_base_of_v<Disconnector, D> &&
            std::is_invocable_v<S, D, Args...>
        >* = nullptr)
    {
        return Connect([&](Args&&... args) {
            (disconnector->*slot)(std::forward<Args>(args)...); },
            disconnector);
    }

    // slot - invocable with Args...
    // disconnector - Disconnector instance
    template <typename S>
    Connection Connect(S&& slot, Disconnector* disconnector,
        typename std::enable_if_t<std::is_invocable_v<S, Args...>>* = nullptr)
    {
        if (disconnector)
        {
            disconnector->AddConnection({ actual_slot_id_, impl_ });

            return Connect(std::forward<S>(slot));
        }

        return { ERROR_ID, std::shared_ptr<detail::ISignal>() };
    }

    // calls all the connected slots
    template <typename... EmitArgs>
    void operator()(EmitArgs&&... args) const
    {
        for (auto&& slot : impl_->slots_)
        {
            // callable target could have been destroyed after Connect()
            if (slot.second)
            {
                slot.second(std::forward<EmitArgs>(args)...);
            }
        }
    }

    // terminates all the connections, removes all the slots
    void DisconnectAll()
    {
        impl_->slots_.clear();
        // actual_slot_id_ = 0; // optionally reset the counter
    }

private:

    class SignalImpl;
    std::shared_ptr<SignalImpl> impl_;

    size_t actual_slot_id_ = 0;
};

} // namespace itom


#endif // __ITOMSIGNAL_H__