# Kinda jank (for now) way to print a message to the user at the top of the screen.

class DriverMessage
  def initialize(text)
    @text = text
  end

  def show(timeout: -1)
    # Simplest possible implementation for now. Maybe use another message program in future.
    args = ['i3-nagbar', '-t', 'warning', '-m', @text]
    if timeout > 0
      args = ['timeout', "#{timeout}"] + args
    end

    @pid ||= spawn(*args)
  end

  def hide
    if @pid
      puts "Killing #{@pid}"
      Process.kill('KILL', @pid)
      @pid = nil
    end
  end
end
