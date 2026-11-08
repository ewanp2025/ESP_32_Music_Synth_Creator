#ifndef MIDIREADER_H
#define MIDIREADER_H

#include <QThread>
#include <QString>
#include <atomic>
#include <functional>

class MidiReader : public QThread {
    Q_OBJECT
public:
    using NoteCallback      = std::function<void(int midiNote, int velocity, bool noteOn)>;
    using CcCallback        = std::function<void(int cc, int value)>;
    using PitchBendCallback = std::function<void(float bendNorm)>;

    explicit MidiReader(NoteCallback cb, QObject* parent = nullptr);
    ~MidiReader() override;

    void setCcCallback(CcCallback cb) { m_ccCallback = std::move(cb); }
    void setPitchBendCallback(PitchBendCallback cb) { m_pbCallback = std::move(cb); }

    void stopReader();

    NoteCallback      m_callback;
    CcCallback        m_ccCallback;
    PitchBendCallback m_pbCallback;

signals:
    void midiStatusChanged(const QString& deviceName);

protected:
    void run() override;

private:
    std::atomic<bool> m_running{true};
};

#endif // MIDIREADER_H