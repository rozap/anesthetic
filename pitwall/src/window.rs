pub struct Window {
  buf: Vec<u16>,
  size: usize,
}

impl Window {
  pub fn new(size: usize) -> Window {
      Window { size: size, buf: vec![0; size] }
  }

  pub fn last(&self) -> u16 {
      self.buf[0]
  }

  pub fn put(&mut self, value: &str) -> Result<(), String> {
      match value.parse::<u16>() {
          Ok(v) => {
              if self.buf.len() >= self.size {
                  self.buf.pop();
              }
              self.buf.insert(0, v);
              Ok(())
          }
          Err(e) => Err(format!("Failed to parse as uint: {}", e)),
      }
  }

  pub fn last_n(&self, n: usize) -> Vec<(f64, u16)> {
      self.buf[0..n.min(self.buf.len())].iter().rev().enumerate().map(|(i, v)| {
          (i as f64, v.to_owned())
      }).collect()
  }
}
