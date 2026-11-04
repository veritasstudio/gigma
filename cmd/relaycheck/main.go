// relaycheck: smoke-test the host→browser audio relay end to end.
//
//	go run ./cmd/relaycheck -base https://gigma.veritas.org.in
//
// Creates a session, claims host, opens the browser WebSocket, sends PCM16
// packets to the relay UDP address the broker advertises, and expects them back.
package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"net"
	"net/http"
	"os"
	"strings"
	"time"

	"github.com/coder/websocket"
)

func post(base, path string, body any, out any) error {
	b, _ := json.Marshal(body)
	res, err := http.Post(base+path, "application/json", bytes.NewReader(b))
	if err != nil {
		return err
	}
	defer res.Body.Close()
	if res.StatusCode/100 != 2 {
		return fmt.Errorf("%s: HTTP %d", path, res.StatusCode)
	}
	return json.NewDecoder(res.Body).Decode(out)
}

func main() {
	base := flag.String("base", "http://127.0.0.1:18080", "broker base URL")
	relayOverride := flag.String("relay", "", "UDP relay host:port (default: what the broker advertises)")
	flag.Parse()

	var sess struct{ ID string }
	must(post(*base, "/api/sessions", map[string]string{"title": "relaycheck"}, &sess))
	var claim struct {
		Host  struct{ HostID string }
		Relay string
	}
	must(post(*base, "/api/sessions/"+sess.ID+"/host", map[string]string{"aooEndpoint": "192.0.2.1:1"}, &claim))
	relay := claim.Relay
	if *relayOverride != "" {
		relay = *relayOverride
	}
	if relay == "" {
		fail("broker advertises no relay (GIGMA_RELAY_PUBLIC unset)")
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	wsURL := strings.Replace(*base, "http", "ws", 1) + "/api/sessions/" + sess.ID + "/listen"
	c, _, err := websocket.Dial(ctx, wsURL, nil)
	must(err)
	defer c.CloseNow()
	time.Sleep(300 * time.Millisecond)

	conn, err := net.Dial("udp", relay)
	must(err)
	pkt := make([]byte, 48+240*2*2)
	copy(pkt, "GGM1")
	copy(pkt[4:], sess.ID)
	copy(pkt[20:], claim.Host.HostID)
	binary.LittleEndian.PutUint32(pkt[40:], 48000)
	pkt[44] = 2
	go func() {
		for i := 0; i < 50; i++ {
			binary.LittleEndian.PutUint32(pkt[36:], uint32(i))
			conn.Write(pkt)
			time.Sleep(5 * time.Millisecond)
		}
	}()
	got := 0
	for got < 40 {
		_, data, err := c.Read(ctx)
		if err != nil {
			break
		}
		if len(data) == 12+240*2*2 && binary.LittleEndian.Uint32(data[4:]) == 48000 {
			got++
		}
	}
	fmt.Printf("session=%s relay=%s frames=%d\n", sess.ID, relay, got)
	if got < 40 {
		fail("relay lost too many frames")
	}
	fmt.Println("PASS")
}

func must(err error) {
	if err != nil {
		fail(err.Error())
	}
}

func fail(msg string) {
	fmt.Println("FAIL:", msg)
	os.Exit(1)
}
