#! /usr/bin/env ruby
require 'open3'

# Takes a shell command. Runs it. Spawns a new copy if that
# spawned process dies (not automatically, someone has to call
# start_if_not_running).
class ProcessKeepAlive
  # command: Same arguments as Open3.popen2.
  def initialize(command:)
    @command = command
  end

  def start_if_not_running
    if @wait_thr.nil? || !@wait_thr.alive?
      if @wait_thr
        @stdin.close
        @stdout.close
        puts "Subprocess exit code: #{@wait_thr.value}"
        puts "Command was: #{@command}"
      end
      @stdin, @stdout, @wait_thr = Open3.popen2(*@command)
    end
  end
end
